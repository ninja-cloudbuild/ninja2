//
// Created by ubuntu on 23-6-18.
//

#ifndef NINJA_SHARE_THREAD_H
#define NINJA_SHARE_THREAD_H

#include <memory>
#include <atomic>
#include <string>
#include <vector>
#include <queue>
#include <signal.h>
#include <sys/resource.h>
#include <grpcpp/grpcpp.h>
#include "proxy.grpc.pb.h"
#include "common.pb.h"
#include "proxy_service_client.h"
#include "../graph.h"
#include "../exit_status.h"
#include "../rbe_config.h"
#include "share_worker.h"

using namespace std;

struct ShareThread {
public:
    ~ShareThread();

    ExitStatus Finish();

    bool Done() const;

    const string& GetOutput() const;
    const struct rusage* GetUsage() const;
    int exit_code_;
    bool is_done_;
    string result_output;

    void SetResult(int exit_code, std::string output) {
        exit_code_ = exit_code;
        result_output = output;
        is_done_ = true;
    }
private:
    ShareThread(bool use_console, const ProjectConfig& config);
    bool Start(struct ShareThreadSet* set, const std::string& command);
    void OnPipeReady();

    string buf_;

    int fd_;
    pid_t pid_;
    struct rusage rusage_;

    bool use_console_;

    friend struct ShareThreadSet;

    const ProjectConfig& rbe_config_;
};



class RemoteCommandDispatcher {
    private:
        ShareWorkerPool thread_pool_;
        // Store pointers to CompletionQueue instead of the objects directly(copyable)
        std::vector<std::unique_ptr<grpc::CompletionQueue>> cqs_; // CompletionQueue is not copyable
        std::vector<std::unique_ptr<AsyncProxyClient>> async_clients_;
        std::atomic<size_t> next_client_{0};

    public:
        RemoteCommandDispatcher(const std::string& server_address, int thread_count = 16)
            : thread_pool_(thread_count) {
            async_clients_.reserve(thread_count);
            // Create CompletionQueues using unique_ptr
            for (int i = 0; i < thread_count; i++) {
                cqs_.push_back(std::make_unique<grpc::CompletionQueue>());
                async_clients_.push_back(
                    std::make_unique<AsyncProxyClient>(
                        grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()),
                        cqs_.back().get()
                    )
                );

                thread_pool_.Enqueue([this, i] {
                    async_clients_[i]->ProcessQueue();
                });
            }
        }

        bool SendCommand(ShareThread* st, const std::string& cmd_id, const std::string& command, const ProjectConfig& config);
};

struct ShareThreadSet {
    ShareThreadSet(const ProjectConfig& config);
    ~ShareThreadSet();

    ShareThread* Add(const EdgeCommand& cmd, const ProjectConfig& config);
    bool DoWork();
    ShareThread* NextFinished();
    void Clear();

    vector<ShareThread*> running_;
    queue<ShareThread*> finished_;


    static void SetInterruptedFlag(int signum);
    static void HandlePendingInterruption();
    /// Store the signal number that causes the interruption.
    /// 0 if not interruption.
    static int interrupted_;

    static bool IsInterrupted() { return interrupted_ != 0; }

    struct sigaction old_int_act_;
    struct sigaction old_term_act_;
    struct sigaction old_hup_act_;
    sigset_t old_mask_;

    int task_id = 0;
    std::unique_ptr<RemoteCommandDispatcher> system_;
};

#endif //NINJA_SHARE_THREAD_H
