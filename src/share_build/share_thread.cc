//
// Created by ubuntu on 23-6-18.
//

#include "share_thread.h"
#include <netinet/in.h>
#include <errno.h>
#include <poll.h>
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
    : fd_(-1), pid_(-1), use_console_(use_console), rbe_config_(config) {}
    

ShareThread::~ShareThread() {
    if (fd_ >= 0)
        close(fd_);
    // Reap child if forgotten.
    if (pid_ != -1)
        Finish();
}

void work(ShareThread &ShareThread, const ProjectConfig& rbe_config, string cmd_id, string cmd) {
    string result_output = ShareExecute(rbe_config, cmd_id, cmd);
    ShareThread.result_output = result_output;
}

bool ShareThread::Start(ShareThreadSet* set, const string& command) {
    set->task_id ++;
    std::string cmd_id = rbe_config_.self_ipv4_addr + "_" + to_string(set->task_id);
    thread t(work, ref(*this), rbe_config_, cmd_id, command);
    t.detach();
    return true;
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
    return ExitSuccess;
    // assert(pid_ != -1);
    // int status;
    // if (wait4(pid_, &status, 0, &rusage_) < 0)
    //   Fatal("wait4(%d): %s", pid_, strerror(errno));
    // pid_ = -1;

    // if (WIFEXITED(status)) {
    //   int exit = WEXITSTATUS(status);
    //   if (exit == 0)
    //     return ExitSuccess;
    // } else if (WIFSIGNALED(status)) {
    //   if (WTERMSIG(status) == SIGINT || WTERMSIG(status) == SIGTERM
    //       || WTERMSIG(status) == SIGHUP)
    //     return ExitInterrupted;
    // }
    // return ExitFailure;
}

bool ShareThread::Done() const {
    return fd_ == -1;
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

ShareThreadSet::ShareThreadSet() {
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
        if ((*i)->result_output != "") {
            //说明*i已经执行完成
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
