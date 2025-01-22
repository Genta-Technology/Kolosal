# Adding a New Model to Kolosal AI

This guide explains how to add a new model to the Kolosal AI application by creating a `.json` configuration file. Follow the steps below:

---

## Steps to Add a New Model

### 1. Copy an Existing JSON File
- Locate an existing `.json` file in the models directory, such as `gemma-2-2b.json`.
- Duplicate the file and rename it to match your new model's name, e.g., `new-model-name.json`.

```bash
cp gemma-2-2b.json new-model-name.json
```

---

### 2. Edit the JSON File
- Open the newly copied `.json` file in your preferred text editor.
- Update the following fields:

#### **a. Fill in the `name` and `author` fields**
Provide the name of the model and its author:

```json
"name": "New Model Name",
"author": "Author Name",
```

#### **b. Fill in the paths for model precisions**
Each precision (Full Precision, 8-bit Quantized, 4-bit Quantized) can be configured. Update the `path` and `downloadLink` fields as follows:

- **`path`**: Set the path where the model is located or will be downloaded to on your disk.
- **`downloadLink`**: Provide the URL from which Kolosal AI can download the model.

If a specific precision is not available, **leave the `path` or `downloadLink` fields empty or leave them as they are. Do not remove the precision section.**

Example:

```json
"fullPrecision": {
  "type": "Full Precision",
  "path": "models/new-model-name/fp16/new-model-fp16.gguf",
  "downloadLink": "https://huggingface.co/kolosal/new-model/resolve/main/new-model-fp16.gguf",
  "isDownloaded": false,
  "downloadProgress": 0.0,
  "lastSelected": 0
},
"quantized8Bit": {
  "type": "8-bit Quantized",
  "path": "",
  "downloadLink": "",
  "isDownloaded": false,
  "downloadProgress": 0.0,
  "lastSelected": 0
},
"quantized4Bit": {
  "type": "4-bit Quantized",
  "path": "models/new-model-name/int4/new-model-Q4_K_M.gguf",
  "downloadLink": "https://huggingface.co/kolosal/new-model/resolve/main/new-model-Q4_K_M.gguf",
  "isDownloaded": false,
  "downloadProgress": 0.0,
  "lastSelected": 0
}
```

---

### 3. Save the JSON File
- After making the necessary changes, save the `.json` file.

---

### 4. Verify the Model Configuration
- Start the Kolosal AI application and ensure the new model appears in the model selection menu.
- Check that the model can be downloaded and loaded without issues.

---

## Example JSON Template
Here is a complete example JSON configuration for reference:

```json
{
  "name": "New Model Name",
  "author": "Author Name",
  "fullPrecision": {
    "type": "Full Precision",
    "path": "models/new-model-name/fp16/new-model-fp16.gguf",
    "downloadLink": "https://huggingface.co/kolosal/new-model/resolve/main/new-model-fp16.gguf",
    "isDownloaded": false,
    "downloadProgress": 0.0,
    "lastSelected": 0
  },
  "quantized8Bit": {
    "type": "8-bit Quantized",
    "path": "",
    "downloadLink": "",
    "isDownloaded": false,
    "downloadProgress": 0.0,
    "lastSelected": 0
  },
  "quantized4Bit": {
    "type": "4-bit Quantized",
    "path": "models/new-model-name/int4/new-model-Q4_K_M.gguf",
    "downloadLink": "https://huggingface.co/kolosal/new-model/resolve/main/new-model-Q4_K_M.gguf",
    "isDownloaded": false,
    "downloadProgress": 0.0,
    "lastSelected": 0
  }
}
```

---

## Notes
- Ensure that all file paths and download links are correct to avoid errors during model download or loading.
- The `isDownloaded` and `downloadProgress` fields should remain as `false` and `0.0`, respectively. These will be updated automatically by Kolosal AI.
- Keep your JSON file well-formatted to avoid parsing errors.
- You can now open your compiled Kolosal AI application without have to recompile it.

---

For additional support, contact the Kolosal AI team or refer to the official documentation.
