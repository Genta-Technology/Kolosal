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

	bool isValid() const;
};

/**
 * @brief Result of a completion job.
 */
struct CompletionResult
{
	std::vector<int32_t> tokens;
	std::string text;
};

#endif // TYPES_H