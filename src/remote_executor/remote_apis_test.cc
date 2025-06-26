#include "cas_client.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>
#include <uuid.h>

namespace
{

	using namespace build::bazel::remote::execution::v2;
	using namespace google::longrunning;
	using namespace google::bytestream;

	class RemoteApiTest : public ::testing::Test
	{
	  protected:
		void SetUp() override
		{
			// The address of buildbuddy, can be modified if needed
			std::string endpoint = "127.0.0.1:1985";

			channel_ = grpc::CreateChannel(endpoint,
			                               grpc::InsecureChannelCredentials());
			cas_stub_ = ContentAddressableStorage::NewStub(channel_);
			ac_stub_ = ActionCache::NewStub(channel_);
			exec_stub_ = Execution::NewStub(channel_);
			bytestream_stub_ = ByteStream::NewStub(channel_);
			// set unique uuid
			uuid_t uu;
			uuid_generate(uu);
			uuid_ = std::string(36, 0);
			uuid_unparse_lower(uu, &uuid_[0]);
		}

		inline Digest MakeDigest(const std::string& content)
		{
			return RemoteExecutor::CASHash::Hash(content);
		}

		std::shared_ptr<grpc::Channel>             channel_;
		std::unique_ptr<ByteStream::StubInterface> bytestream_stub_;
		std::unique_ptr<ContentAddressableStorage::StubInterface> cas_stub_;
		std::unique_ptr<ActionCache::StubInterface>               ac_stub_;
		std::unique_ptr<Execution::StubInterface>                 exec_stub_;
		std::string instance_name_;
		std::string uuid_;
	};

}   // namespace

// Action cache Miss Test
TEST_F(RemoteApiTest, GetActionResultOnCacheMiss)
{
	// 1. Random Action Digest, which has a high possibility of cache miss
	Action action;
	action.set_do_not_cache(true);
	std::string action_blob;
	action.SerializeToString(&action_blob);
	Digest action_digest = MakeDigest(action_blob);

	// 2. Query Action cache
	grpc::ClientContext    context;
	GetActionResultRequest request;
	request.set_instance_name(instance_name_);
	request.mutable_action_digest()->CopyFrom(action_digest);
	ActionResult response;
	grpc::Status status =
		ac_stub_->GetActionResult(&context, request, &response);

	// 3. Verify return status is NOT_FOUND
	ASSERT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
}

// Action cache Hit Test
// ❗ Note: This test assumes that the Action cache is empty before running it.
TEST_F(RemoteApiTest, UpdateAndGetActionResultOnCacheHit)
{
	// 1. Prepare the Action and the corresponding Action Result
	Command command;
	command.add_arguments("echo");
	command.add_arguments("hit");
	std::string command_blob;
	command.SerializeToString(&command_blob);
	Digest command_digest = MakeDigest(command_blob);

	Action action;
	action.mutable_command_digest()->CopyFrom(command_digest);
	std::string action_blob;
	action.SerializeToString(&action_blob);
	Digest action_digest = MakeDigest(action_blob);

	ActionResult result;
	result.set_exit_code(0);
	result.set_stdout_raw("hit");

	// 2. Query, expecting cache miss, and the return status is NOT_FOUND
	{
		grpc::ClientContext    context;
		GetActionResultRequest request;
		request.set_instance_name(instance_name_);
		request.mutable_action_digest()->CopyFrom(action_digest);
		ActionResult response;
		grpc::Status status =
			ac_stub_->GetActionResult(&context, request, &response);

		ASSERT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
	}

	// 3. Update Action cache
	{
		grpc::ClientContext       context;
		UpdateActionResultRequest request;
		request.set_instance_name(instance_name_);
		request.mutable_action_digest()->CopyFrom(action_digest);
		*request.mutable_action_result() = result;
		ActionResult response;
		grpc::Status status =
			ac_stub_->UpdateActionResult(&context, request, &response);
		ASSERT_TRUE(status.ok()) << "Remote_api-UpdateActionResult failed: "
								 << status.error_message();
	}

	// 4. Query again, and expect cache hit
	{
		grpc::ClientContext    context;
		GetActionResultRequest request;
		request.set_instance_name(instance_name_);
		request.mutable_action_digest()->CopyFrom(action_digest);
		ActionResult response;
		grpc::Status status =
			ac_stub_->GetActionResult(&context, request, &response);
		ASSERT_TRUE(status.ok())
			<< "Remote_api-GetActionResult failed: " << status.error_message();

		EXPECT_EQ(response.exit_code(), 0);
		EXPECT_EQ(response.stdout_raw(), "hit");
	}
}

// CAS's query mechanism Test
TEST_F(RemoteApiTest, FindMissingBlobs)
{
	// 1. Prepare an existing data block and a non-existing data block
	std::string existing_content =
		"RemoteApiTest.FindMissingBlobs: This blob exists.";
	Digest      existing_digest = MakeDigest(existing_content);
	std::string missing_content =
		"RemoteApiTest.FindMissingBlobs: This blob is missing.";
	Digest missing_digest = MakeDigest(missing_content);

	// 2. Upload the existing data blocks
	{
		grpc::ClientContext     context;
		BatchUpdateBlobsRequest request;
		request.set_instance_name(instance_name_);
		auto request_blob = request.add_requests();
		request_blob->mutable_digest()->CopyFrom(existing_digest);
		request_blob->set_data(existing_content);

		BatchUpdateBlobsResponse response;
		grpc::Status             status =
			cas_stub_->BatchUpdateBlobs(&context, request, &response);

		ASSERT_TRUE(status.ok());
		ASSERT_EQ(response.responses_size(), 1);
	}

	// 3. Query two data blocks
	{
		grpc::ClientContext     find_context;
		FindMissingBlobsRequest find_request;
		find_request.set_instance_name(instance_name_);
		find_request.add_blob_digests()->CopyFrom(existing_digest);
		find_request.add_blob_digests()->CopyFrom(missing_digest);

		FindMissingBlobsResponse find_response;
		grpc::Status             find_status = cas_stub_->FindMissingBlobs(
            &find_context, find_request, &find_response);

		// 4. Verification results,
		//    the server should return only a lack of digest
		ASSERT_TRUE(find_status.ok());
		ASSERT_EQ(find_response.missing_blob_digests_size(), 1);
		EXPECT_EQ(find_response.missing_blob_digests(0).hash(),
		          missing_digest.hash());
		EXPECT_EQ(find_response.missing_blob_digests(0).size_bytes(),
		          missing_digest.size_bytes());
	}
}

//  Basic data upload and download of CAS Test
TEST_F(RemoteApiTest, BatchUpdateAndReadBlobs)
{
	// 1. Prepare the data to be uploaded
	std::string content = "RemoteApiTest.BatchUpdateAndReadBlobs: Hello!";
	Digest      digest = MakeDigest(content);

	// 2. Upload data block
	{
		grpc::ClientContext     context;
		BatchUpdateBlobsRequest request;
		request.set_instance_name(instance_name_);
		auto request_blob = request.add_requests();
		request_blob->mutable_digest()->CopyFrom(digest);
		request_blob->set_data(content);

		BatchUpdateBlobsResponse response;
		grpc::Status             status =
			cas_stub_->BatchUpdateBlobs(&context, request, &response);
		ASSERT_TRUE(status.ok())
			<< "BatchUpdateBlobs failed: " << status.error_message();
		ASSERT_EQ(response.responses_size(), 1);
		ASSERT_EQ(response.responses(0).status().code(), grpc::StatusCode::OK);
	}

	// 3. Download data block
	{
		grpc::ClientContext   context;
		BatchReadBlobsRequest request;
		request.set_instance_name(instance_name_);
		request.add_digests()->CopyFrom(digest);

		BatchReadBlobsResponse response;
		grpc::Status           status =
			cas_stub_->BatchReadBlobs(&context, request, &response);
		ASSERT_TRUE(status.ok())
			<< "BatchReadBlobs failed: " << status.error_message();
		ASSERT_EQ(response.responses_size(), 1);
		ASSERT_EQ(response.responses(0).status().code(), grpc::StatusCode::OK);

		// 4. Verify the downloaded content
		EXPECT_EQ(response.responses(0).data(), content);
		EXPECT_EQ(response.responses(0).digest().hash(), digest.hash());
	}
}

// The writing and reading of ByteStream Test
TEST_F(RemoteApiTest, ByteStreamWriteAndRead)
{
	// 1. Prepare the content to be uploaded
	std::string content = "RemoteApiTest.ByteStreamWriteAndRead: This is the "
						  "content for the bytestream "
						  "write-read cycle test.";
	Digest      digest = MakeDigest(content);

	const std::string upload_resource_name =
		"/uploads/" + uuid_ + "/blobs/" + digest.hash() + "/" +
		std::to_string(digest.size_bytes());

	// 2. Upload content
	{
		grpc::ClientContext               context;
		google::bytestream::WriteResponse response;
		auto writer = bytestream_stub_->Write(&context, &response);
		ASSERT_TRUE(writer);

		google::bytestream::WriteRequest request;
		request.set_resource_name(upload_resource_name);
		request.set_data(content);
		request.set_finish_write(true);
		ASSERT_TRUE(writer->Write(request));

		writer->WritesDone();
		grpc::Status status = writer->Finish();

		ASSERT_TRUE(status.ok())
			<< "ByteStream.Write failed: " << status.error_message();
		EXPECT_EQ(response.committed_size(), digest.size_bytes());
	}

	const std::string download_resource_name =
		"/blobs/" + digest.hash() + "/" + std::to_string(digest.size_bytes());

	// 3. Download content
	std::string downloaded_content;
	{
		grpc::ClientContext             context;
		google::bytestream::ReadRequest request;
		request.set_resource_name(download_resource_name);
		request.set_read_offset(0);

		auto reader = bytestream_stub_->Read(&context, request);
		ASSERT_TRUE(reader);

		google::bytestream::ReadResponse response;
		while (reader->Read(&response))
		{
			downloaded_content += response.data();
		}

		grpc::Status status = reader->Finish();
		ASSERT_TRUE(status.ok())
			<< "ByteStream.Read failed: " << status.error_message();
	}

	// 4. Verify the downloaded content
	EXPECT_EQ(downloaded_content, content);
}

// Asynchronous Execute operation Test
TEST_F(RemoteApiTest, AsyncExecuteAndReadOperation)
{
	// 1. Prepare the Action and Command
	Command command;
	command.add_arguments("/bin/sh");
	command.add_arguments("-c");
	command.add_arguments("\"echo 'RemoteApiTest.AsyncExecuteAndReadOperation: "
	                      "Hello!' && exit 0\"");
	command.add_output_files("stdout");

	std::string command_blob;
	command.SerializeToString(&command_blob);
	Digest command_digest = MakeDigest(command_blob);

	Directory   empty_dir;
	std::string empty_dir_blob;
	empty_dir.SerializeToString(&empty_dir_blob);
	Digest input_root_digest = MakeDigest(empty_dir_blob);

	Action action;
	*action.mutable_command_digest() = command_digest;
	*action.mutable_input_root_digest() = input_root_digest;
	action.set_do_not_cache(true);

	std::string action_blob = action.SerializeAsString();
	Digest      action_digest = MakeDigest(action_blob);

	// 2. Upload all the data blocks required for execution to CAS
	{
		grpc::ClientContext     context;
		BatchUpdateBlobsRequest request;
		request.set_instance_name(instance_name_);
		auto* cmd_req = request.add_requests();
		*cmd_req->mutable_digest() = command_digest;
		cmd_req->set_data(command_blob);
		auto* dir_req = request.add_requests();
		*dir_req->mutable_digest() = input_root_digest;
		dir_req->set_data(empty_dir_blob);
		auto* action_req = request.add_requests();
		*action_req->mutable_digest() = action_digest;
		action_req->set_data(action_blob);

		BatchUpdateBlobsResponse response;
		grpc::Status             status =
			cas_stub_->BatchUpdateBlobs(&context, request, &response);
		ASSERT_TRUE(status.ok());
	}

	// 3. Execute Action asynchronously
	ExecuteRequest exec_req;
	exec_req.set_instance_name(instance_name_);
	exec_req.set_skip_cache_lookup(true);
	*exec_req.mutable_action_digest() = action_digest;

	grpc::ClientContext   context;
	grpc::CompletionQueue cq;

	auto reader = exec_stub_->AsyncExecute(&context, exec_req, &cq, (void*)1);

	void* tag;
	bool  ok = false;

	// Wait for the initial connection to be completed
	ASSERT_TRUE(cq.Next(&tag, &ok));
	ASSERT_EQ(tag, (void*)1);
	ASSERT_TRUE(ok);

	Operation op;
	bool      completed = false;

	reader->Read(&op, (void*)2);

	while (cq.Next(&tag, &ok))
	{
		ASSERT_EQ(tag, (void*)2);
		if (!ok) break;

		if (op.done())
		{
			completed = true;
			break;
		}
		// Read the update of the next operation
		reader->Read(&op, (void*)2);
	}
	ASSERT_TRUE(completed);

	// End RPC
	grpc::Status final_status;
	reader->Finish(&final_status, (void*)3);
	ASSERT_TRUE(cq.Next(&tag, &ok));
	ASSERT_EQ(tag, (void*)3);
	ASSERT_TRUE(ok);
	ASSERT_TRUE(final_status.ok());

	// 4. Verify the downloaded content
	ASSERT_TRUE(op.response().Is<ExecuteResponse>());
	ExecuteResponse exec_resp;
	op.response().UnpackTo(&exec_resp);
	ASSERT_EQ(exec_resp.status().code(), grpc::StatusCode::OK);

	ActionResult result = exec_resp.result();
	EXPECT_EQ(result.exit_code(), 0);

	std::string stdout_content;
	if (!result.stdout_raw().empty())
		stdout_content = result.stdout_raw();
	else if (result.has_stdout_digest())
	{
		grpc::ClientContext   read_context;
		BatchReadBlobsRequest read_req;
		read_req.set_instance_name(instance_name_);
		*read_req.add_digests() = result.stdout_digest();

		BatchReadBlobsResponse read_resp;
		grpc::Status           read_status =
			cas_stub_->BatchReadBlobs(&read_context, read_req, &read_resp);

		ASSERT_TRUE(read_status.ok()) << "BatchReadBlobs for stdout failed: "
									  << read_status.error_message();
		ASSERT_EQ(read_resp.responses_size(), 1);
		ASSERT_EQ(read_resp.responses(0).status().code(), grpc::StatusCode::OK);
		stdout_content = read_resp.responses(0).data();
	}

	EXPECT_EQ(stdout_content, "RemoteApiTest.AsyncExecuteAndReadOperation: "
	                          "Hello!\n");
}

int main(int argc, char** argv)
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
