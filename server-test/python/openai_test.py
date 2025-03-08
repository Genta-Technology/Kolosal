import openai
import os

# Configure the client to use your local endpoint
client = openai.OpenAI(
    base_url="http://localhost:8080/v1",
    api_key="sk-dummy"  # Using dummy API key as in the curl example
)

print("Starting streaming request...\n")

# Make a streaming request
stream = client.chat.completions.create(
    model="claude-3-opus-20240229",
    messages=[
        {"role": "system", "content": "You are a helpful assistant."},
        {"role": "user", "content": "Why anything to the power of zero is 1?"}
    ],
    stream=False
)

# Process the full_response
print("Full response:")
print(stream.choices[0].message.content)

# Process streaming response
# print("Streaming response:")
# full_response = ""
# for chunk in stream:
#     if chunk.choices[0].delta.content is not None:
#         content = chunk.choices[0].delta.content
#         full_response += content
#         print(content, end="", flush=True)

# print("\n\nFull response:", full_response)

