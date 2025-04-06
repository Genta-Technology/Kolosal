import openai
import os

# Configure the client to use your local endpoint
client = openai.OpenAI(
    base_url="http://localhost:8080/v1",
    api_key="sk-dummy"  # Using dummy API key as in the curl example
)

print("Starting non-streaming request...\n")

# Format the messages into a single text prompt
system_message = "You are a helpful assistant."
user_message = "Why anything to the power of zero is 1?"
prompt = f"{system_message}\n\nUser: {user_message}\nAssistant:"

# Make a non-streaming request using completions API
response = client.completions.create(
    model="Qwen2.5 0.5B",
    prompt=prompt,
    max_tokens=32
)

# Process the response
full_response = response.choices[0].text
print("Response:")
print(full_response)
