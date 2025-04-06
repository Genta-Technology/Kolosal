import openai
import os

# Configure the client to use your local endpoint
client = openai.OpenAI(
    base_url="http://localhost:8080/v1",
    api_key="sk-dummy"  # Using dummy API key as in the curl example
)

print("Starting streaming request...\n")

prompt = f"Halo adalah"

# Make a streaming request using completions API instead of chat
stream = client.completions.create(
    model="Qwen2.5 0.5B",
    prompt=prompt,
    stream=True,
    max_tokens=32
)

# Process streaming response
print("Streaming response:")
full_response = ""
for chunk in stream:
    if chunk.choices[0].text is not None:
        content = chunk.choices[0].text
        full_response += content
        print(content, end="", flush=True)

print("\n\nFull response:", full_response)
