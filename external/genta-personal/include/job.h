#ifndef JOB_H
#define JOB_H

#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <exception>

#include "types.h"
#include "llama.h"
#include "common.h"
#include "sampling.h"

struct Job {
    int jobId;
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<int32_t> generatedTokens;
    std::string generatedText;
    bool isFinished = false;
    bool hasError = false;
    std::string errorMessage;
    float tps = 0;
    std::atomic<bool> cancelRequested{ false };
    CompletionParameters params;

    int seqId;

    bool isDecodingPrompt = true;

    int n_past;
    int n_remain;
    int i_prompt;
    int n_prompt;
    size_t n_matching_session_tokens;

    std::vector<llama_token> session_tokens;
    std::vector<llama_token> embd_inp;
    std::string path_session;
    struct common_sampler* smpl = nullptr;
    int batch_pos = 0;

    ~Job() {
        if (smpl) {
            common_sampler_free(smpl);
        }
    }
};

#endif // JOB_H