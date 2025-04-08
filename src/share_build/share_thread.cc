//
// Created by ubuntu on 23-6-18.
//

#include "share_thread.h"
#include <netinet/in.h>
#include <errno.h>
#include <poll.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <thread>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include "../util.h"

#include <grpcpp/grpcpp.h>
#include "proxy.grpc.pb.h"
#include "common.pb.h"
#include "proxy_service_client.h"
#include "sharebuild.h"


extern char** environ;


ShareThread::ShareThread(bool use_console, const ProjectConfig& config)
    : fd_(-1), pid_(-1), use_console_(use_console), rbe_config_(config),
      exit_code_(-1), is_done_(false) {}

ShareThread::~ShareThread() {
    if (fd_ >= 0)
        close(fd_);
    // Reap child if forgotten.
    if (pid_ != -1)
        Finish();
}

void work(ShareThread &share_thread, const ProjectConfig& rbe_config, string cmd_id, string cmd) {
    std::pair<int, std::string> remote_res = ShareExecute(rbe_config, cmd_id, cmd);
    share_thread.exit_code_ = remote_res.first;
    share_thread.result_output = remote_res.second;
    share_thread.is_done_ = true;
}

bool ShareThread::Start(ShareThreadSet* set, const string& command) {
    set->task_id ++;
    std::string cmd_id = rbe_config_.self_ipv4_addr + "_" + to_string(set->task_id);
    return set->system_->SendCommand(this, cmd_id, command, rbe_config_);
}

void ShareThread::OnPipeReady() {
    char buf[4 << 10];
    ssize_t len = read(fd_, buf, sizeof(buf));
    if (len > 0) {
        buf_.append(buf, len);
    } else {
        if (len < 0)
            Fatal("read: %s", strerror(errno));
        close(fd_);
        fd_ = -1;
    }
}

const struct rusage* ShareThread::GetUsage() const {
    return &rusage_;
}

ExitStatus ShareThread::Finish() {
    return exit_code_ == 0 ? ExitSuccess : ExitFailure;
}

bool ShareThread::Done() const {
    return is_done_;
}

const string& ShareThread::GetOutput() const {
    return result_output;
}

int ShareThreadSet::interrupted_;

void ShareThreadSet::SetInterruptedFlag(int signum) {
    interrupted_ = signum;
}

void ShareThreadSet::HandlePendingInterruption() {
    sigset_t pending;
    sigemptyset(&pending);
    if (sigpending(&pending) == -1) {
        perror("ninja: sigpending");
        return;
    }
    if (sigismember(&pending, SIGINT))
        interrupted_ = SIGINT;
    else if (sigismember(&pending, SIGTERM))
        interrupted_ = SIGTERM;
    else if (sigismember(&pending, SIGHUP))
        interrupted_ = SIGHUP;
}

ShareThreadSet::ShareThreadSet(const ProjectConfig& config) {
    size_t thread_count = GetProcessorCount() + 2;
    system_ = std::make_unique<RemoteCommandDispatcher>(config.shareproxy_addr, thread_count);
    char address[INET_ADDRSTRLEN];

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &set, &old_mask_) < 0)
        Fatal("sigprocmask: %s", strerror(errno));

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = SetInterruptedFlag;
    if (sigaction(SIGINT, &act, &old_int_act_) < 0)
        Fatal("sigaction: %s", strerror(errno));
    if (sigaction(SIGTERM, &act, &old_term_act_) < 0)
        Fatal("sigaction: %s", strerror(errno));
    if (sigaction(SIGHUP, &act, &old_hup_act_) < 0)
        Fatal("sigaction: %s", strerror(errno));
}

ShareThreadSet::~ShareThreadSet() {
    Clear();

    if (sigaction(SIGINT, &old_int_act_, 0) < 0)
        Fatal("sigaction: %s", strerror(errno));
    if (sigaction(SIGTERM, &old_term_act_, 0) < 0)
        Fatal("sigaction: %s", strerror(errno));
    if (sigaction(SIGHUP, &old_hup_act_, 0) < 0)
        Fatal("sigaction: %s", strerror(errno));
    if (sigprocmask(SIG_SETMASK, &old_mask_, 0) < 0)
        Fatal("sigprocmask: %s", strerror(errno));
}

ShareThread *ShareThreadSet::Add(const EdgeCommand& cmd, const ProjectConfig& config) {
    ShareThread *shareThread = new ShareThread(cmd.use_console, config);
    if (!shareThread->Start(this, cmd.command)) {
        delete shareThread;
        return 0;
    }
    running_.push_back(shareThread);
    return shareThread;
}

bool ShareThreadSet::DoWork() {
    for (vector<ShareThread*>::iterator i = running_.begin(); i != running_.end(); ) {
        if ((*i)->Done()) {
            finished_.push(*i);
            i = running_.erase(i);
            continue;
        }
        ++i;
    }
    HandlePendingInterruption();
    return IsInterrupted();
}

ShareThread* ShareThreadSet::NextFinished() {
    if (finished_.empty())
        return NULL;
    ShareThread* subproc = finished_.front();
    finished_.pop();
    return subproc;
}

void ShareThreadSet::Clear() {
    for (vector<ShareThread*>::iterator i = running_.begin();
         i != running_.end(); ++i)
        // Since the foreground process is in our process group, it will receive
        // the interruption signal (i.e. SIGINT or SIGTERM) at the same time as us.
        if (!(*i)->use_console_)
            kill(-(*i)->pid_, interrupted_);
    for (vector<ShareThread*>::iterator i = running_.begin();
         i != running_.end(); ++i)
        delete *i;
    running_.clear();
}


bool RemoteCommandDispatcher::SendCommand(ShareThread* st, const std::string& cmd_id, const std::string& command, const ProjectConfig& config) {
    size_t client_index = (next_client_++) % async_clients_.size();
    auto& client = async_clients_[client_index];

    api::ForwardAndExecuteRequest request;
    api::Project project;
    project.set_ninja_host(config.self_ipv4_addr);
    project.set_root_dir(config.project_root);
    project.set_ninja_dir(config.cwd);
    *request.mutable_project() = project;
    request.set_cmd_id(cmd_id);
    request.set_cmd_content(command);

    client->AsyncExecute(request, [st](const api::ForwardAndExecuteResponse& response, grpc::Status status) {
        if (status.ok() && response.status().code() == api::PROXY_OK) {
            std::string result = "stdout: " + response.std_out() + ", stderr: " + response.std_err();
            st->SetResult(0, result);
        } else {
            st->SetResult(-1, "RPC failed or execution error");
        }
    });
    return true;
}