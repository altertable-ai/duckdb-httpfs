#include "catch.hpp"

#include "s3/mock_s3_server.hpp"
#include "s3/s3_test_helper.hpp"

#include "duckdb.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

namespace {

struct FirstChunkDownloadResult {
	string data;
	idx_t file_size;
	vector<MockS3RequestObservation> observations;
};

FirstChunkDownloadResult RunForcedDownload(MockS3ServerConfig config, const string &client, idx_t chunk_size,
                                           idx_t parallel_threshold) {
	config.auth.refresh_target = MockS3RefreshTarget::DELETE_OBJECT;
	MockS3Server server(std::move(config));
	DuckDB db(nullptr);
	Connection con(db);
	S3TestHelper::ConfigureRefresh(db, con, server, client, false);
	S3TestHelper::RequireQueryOk(con, "SET force_download=true");
	S3TestHelper::RequireQueryOk(con, "SET http_download_max_concurrency=4");
	S3TestHelper::RequireQueryOk(con, "SET http_download_chunk_size=" + to_string(chunk_size));
	S3TestHelper::RequireQueryOk(con, "SET http_download_parallel_threshold=" + to_string(parallel_threshold));
	S3TestHelper::RequireQueryOk(con, "BEGIN TRANSACTION");
	auto &fs = FileSystem::GetFileSystem(*con.context);
	auto handle = fs.OpenFile(S3TestHelper::S3_PATH, FileFlags::FILE_FLAGS_READ);
	FirstChunkDownloadResult result;
	result.file_size = handle->GetFileSize();
	result.data = string(result.file_size, '?');
	if (result.file_size > 0) {
		handle->Read(QueryContext(*con.context), &result.data[0], result.file_size, 0);
	}
	S3TestHelper::RequireQueryOk(con, "COMMIT");
	result.observations = server.Observations();
	return result;
}

vector<string> GetRanges(const vector<MockS3RequestObservation> &observations) {
	vector<string> result;
	for (auto &observation : observations) {
		REQUIRE(observation.method == "GET");
		result.push_back(observation.range);
	}
	std::sort(result.begin(), result.end());
	return result;
}

} // namespace

TEST_CASE("Forced downloads complete large files with parallel ranged GETs after the first chunk",
          "[httpfs][s3][first-chunk-download]") {
	for (const string client : {"curl", "httplib"}) {
		CAPTURE(client);
		MockS3ServerConfig config;
		const auto object_data = config.object.data;
		REQUIRE(object_data.size() == 36);
		auto result = RunForcedDownload(config, client, 10, 1);
		INFO(MockS3DescribeObservations(result.observations));
		REQUIRE(result.file_size == object_data.size());
		REQUIRE(result.data == object_data);
		// No HEAD: the first chunk reveals the size, the remaining chunks are pinned to its ETag
		REQUIRE(GetRanges(result.observations) ==
		        vector<string> {"bytes=0-9", "bytes=10-19", "bytes=20-29", "bytes=30-35"});
		REQUIRE(result.observations[0].range == "bytes=0-9");
		REQUIRE(result.observations[0].if_match.empty());
		for (idx_t i = 1; i < result.observations.size(); i++) {
			REQUIRE(result.observations[i].if_match == "\"httpfs-refresh-test-etag\"");
			REQUIRE(result.observations[i].status == 206);
		}
	}
}

TEST_CASE("Forced downloads below the parallel threshold fetch the rest with one GET",
          "[httpfs][s3][first-chunk-download]") {
	for (const string client : {"curl", "httplib"}) {
		CAPTURE(client);
		MockS3ServerConfig config;
		const auto object_data = config.object.data;
		auto result = RunForcedDownload(config, client, 10, 1000);
		INFO(MockS3DescribeObservations(result.observations));
		REQUIRE(result.data == object_data);
		REQUIRE(GetRanges(result.observations) == vector<string> {"bytes=0-9", "bytes=10-35"});
	}
}

TEST_CASE("Forced downloads of files within the first chunk take a single GET",
          "[httpfs][s3][first-chunk-download]") {
	for (const string client : {"curl", "httplib"}) {
		CAPTURE(client);
		MockS3ServerConfig config;
		const auto object_data = config.object.data;
		auto result = RunForcedDownload(config, client, 64, 1);
		INFO(MockS3DescribeObservations(result.observations));
		REQUIRE(result.data == object_data);
		REQUIRE(result.observations.size() == 1);
		REQUIRE(result.observations[0].range == "bytes=0-63");
		REQUIRE(result.observations[0].status == 206);
	}
}

TEST_CASE("Forced downloads keep the whole body when the server ignores the first chunk range",
          "[httpfs][s3][first-chunk-download]") {
	for (const string client : {"curl", "httplib"}) {
		CAPTURE(client);
		MockS3ServerConfig config;
		config.range.behavior = MockS3RangeBehavior::IGNORE_RANGE;
		const auto object_data = config.object.data;
		auto result = RunForcedDownload(config, client, 10, 1);
		INFO(MockS3DescribeObservations(result.observations));
		REQUIRE(result.data == object_data);
		REQUIRE(result.observations.size() == 1);
		REQUIRE(result.observations[0].status == 200);
	}
}

TEST_CASE("Forced downloads of empty files fall back to a full GET", "[httpfs][s3][first-chunk-download]") {
	for (const string client : {"curl", "httplib"}) {
		CAPTURE(client);
		MockS3ServerConfig config;
		config.object.data = "";
		auto result = RunForcedDownload(config, client, 10, 1);
		INFO(MockS3DescribeObservations(result.observations));
		REQUIRE(result.file_size == 0);
		REQUIRE(result.observations.size() == 2);
		REQUIRE(result.observations[0].range == "bytes=0-9");
		REQUIRE(result.observations[0].status == 416);
		REQUIRE(result.observations[1].range.empty());
		REQUIRE(result.observations[1].status == 200);
	}
}

} // namespace duckdb
