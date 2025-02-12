#ifndef INFERENCE_INTERFACE_H
#define INFERENCE_INTERFACE_H

#include "types.h"
#include <string>

#ifdef INFERENCE_EXPORTS
#define INFERENCE_API __declspec(dllexport)
#else
#define INFERENCE_API __declspec(dllimport)
#endif

/**
 * @brief Pure virtual interface for an inference engine.
 */
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;

    virtual bool loadModel(const char* engineDir, const int mainGpuId = -1) = 0;
    virtual bool unloadModel() = 0;
    virtual int submitCompletionsJob(const CompletionParameters& params) = 0;
    virtual int submitChatCompletionsJob(const ChatCompletionParameters& params) = 0;
	virtual void stopJob(int job_id) = 0;
    virtual bool isJobFinished(int job_id) = 0;
    virtual CompletionResult getJobResult(int job_id) = 0;
    virtual void waitForJob(int job_id) = 0;
    virtual bool hasJobError(int job_id) = 0;
    virtual std::string getJobError(int job_id) = 0;
};

// Function type definition for the creation function
typedef IInferenceEngine* (*CreateInferenceEngineFn)();

#endif // INFERENCE_INTERFACE_H