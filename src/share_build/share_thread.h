//
// Created by ubuntu on 23-6-18.
//

#ifndef NINJA_SHARE_THREAD_H
#define NINJA_SHARE_THREAD_H

#include <string>
#include <vector>
#include <queue>
#include <signal.h>
#include <sys/resource.h>
#include "../graph.h"
#include "../exit_status.h"
#include "../rbe_config.h"
using namespace std;

struct ShareThread {
public:
    ~ShareThread();

    ExitStatus Finish();

    bool Done() const;

    const string& GetOutput() const;
    const struct rusage* GetUsage() const;
    string result_output;
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

struct ShareThreadSet {
    ShareThreadSet();
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
};

#endif //NINJA_SHARE_THREAD_H
