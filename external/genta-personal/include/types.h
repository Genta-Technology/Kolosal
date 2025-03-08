#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>

/**
 * @brief Parameters for a completion job.
 */
struct CompletionParameters
{
	std::string prompt;
	int randomSeed = 42;
	int maxNewTokens = 128;
	int minLength = 8;
	float temperature = 1.0f;
	float topP = 0.5f;
	bool streaming = false;
	std::string kvCacheFilePath = "";
	int seqId = -1;

	bool isValid() const;
};

/**
 * @brief Parameters for a chat completion job.
 */
struct Message
{
	std::string role;
	std::string content;
};

/**
 * @brief Parameters for a chat completion job.
 */
struct ChatCompletionParameters
{
	std::vector<Message> messages;
	int randomSeed = 42;
	int maxNewTokens = 128;
	int minLength = 8;
	float temperature = 1.0f;
	float topP = 0.5f;
	bool streaming = false;
	std::string kvCacheFilePath = "";
	int seqId = -1;

	bool isValid() const;
};

/**
 * @brief Result of a completion job.
 */
struct CompletionResult
{
	std::vector<int32_t> tokens;
	std::string text;
	float tps;
};

struct LoadingParameters
{
	int n_ctx = 4096;
	int n_keep = 2048;
	bool use_mlock = true;
	bool use_mmap = false;
	bool cont_batching = true;
	bool warmup = false;
	int n_parallel = 1;
	int n_gpu_layers = 100;
};

#endif // TYPES_H