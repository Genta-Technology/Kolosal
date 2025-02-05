#ifndef INFERENCE_H
#define INFERENCE_H

#include "inference_interface.h"
#include "types.h"

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <exception>

#ifdef INFERENCE_EXPORTS
#define INFERENCE_API __declspec(dllexport)
#else
#define INFERENCE_API __declspec(dllimport)
#endif

/**
 * @brief Interface for an inference engine.
 *
 * This class provides an interface for submitting completion jobs to an inference engine.
 * The engine can be implemented using a CPU or GPU.
 *
 * The engine is responsible for managing the completion jobs and returning the results.
 * 
 * 
 */
class INFERENCE_API InferenceEngine : public IInferenceEngine
{
public:
	explicit InferenceEngine();

	bool loadModel(const char* engineDir, const int mainGpuId = -1);

	/**
	 * @brief Submits a completion job and returns the job ID.
	 * @param params The parameters for the completion job.
	 * @return The ID of the submitted job.
	 */
	int submitCompletionsJob(const CompletionParameters& params);

	/**
	 * @brief Submits a chat completion job and returns the job ID.
	 * @param params The parameters for the chat completion job.
	 * @return The ID of the submitted job.
	 */
	int submitChatCompletionsJob(const ChatCompletionParameters& params);

	/**
	 * @brief Stops a job.
	 * @param job_id The ID of the job to stop.
	 * @return True if the job was stopped, false otherwise.
	 */
	void stopJob(int job_id);

	/**
	 * @brief Checks if a job is finished.
	 * @param job_id The ID of the job to check.
	 * @return True if the job is finished, false otherwise.
	 */
	bool isJobFinished(int job_id);

	/**
	 * @brief Gets the current result of a job.
	 * @param job_id The ID of the job to get the result for.
	 * @return The result of the job.
	 * @note This function would return any results that are currently available, even if the job is not finished.
	 */
	CompletionResult getJobResult(int job_id);

	/**
	 * @brief Waits for a job to finish.
	 * @param job_id The ID of the job to wait for.
	 */
	void waitForJob(int job_id);

	/**
	 * @brief Checks if a job has an error.
	 * @param job_id The ID of the job to check.
	 * @return True if the job has an error, false otherwise.
	 */
	bool hasJobError(int job_id);

	/**
	 * @brief Gets the error message for a job.
	 * @param job_id The ID of the job to get the error message for.
	 * @return The error message for the job.
	 */
	std::string getJobError(int job_id);

	/**
	 * @brief Destructor for the InferenceEngine.
	 */
	~InferenceEngine();

private:
	struct Impl;
	std::unique_ptr<Impl> pimpl;
};

extern "C" INFERENCE_API IInferenceEngine* createInferenceEngine();
extern "C" INFERENCE_API void destroyInferenceEngine(IInferenceEngine* engine);

#endif // INFERENCE_H