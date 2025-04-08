#ifndef GGUF_READER_H
#define GGUF_READER_H

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <memory>
#include <curl/curl.h>
#include <sstream>
#include <cstring>
#include <algorithm>

// Structure to hold the extracted model parameters
struct GGUFModelParams {
    uint64_t hidden_size = 0;       // Mapped from embedding_length
    uint32_t attention_heads = 0;   // Mapped from attention.head_count
    uint32_t hidden_layers = 0;     // Mapped from block_count
    uint32_t kv_heads = 0;          // Mapped from attention.head_count_kv or head_count
};

// Abstract base class for data sources
class DataSource {
public:
    virtual ~DataSource() = default;
    virtual bool read(char* buffer, size_t size) = 0;
    virtual bool seek(size_t position) = 0;
    virtual bool eof() const = 0;
    virtual size_t tell() = 0; // Removed the const qualifier since tellg() is non-const.
};

// File-based data source
class FileDataSource : public DataSource {
public:
    FileDataSource(const std::string& filename) {
        file.open(filename, std::ios::binary);
        if (!file)
            throw std::runtime_error("Failed to open file: " + filename);
    }

    ~FileDataSource() override {
        if (file.is_open())
            file.close();
    }

    bool read(char* buffer, size_t size) override {
        file.read(buffer, size);
        return file.good() || (file.eof() && file.gcount() > 0);
    }

    bool seek(size_t position) override {
        file.seekg(position);
        return file.good();
    }

    bool eof() const override {
        return file.eof();
    }

    size_t tell() override {
        return file.tellg();
    }

private:
    std::ifstream file;
};

// CURL callback data structure
struct CurlBuffer {
    char* buffer;
    size_t size;
    size_t pos;
    bool* abort_download;
};

// CURL-based URL data source
class UrlDataSource : public DataSource {
public:
    UrlDataSource(const std::string& url) : url(url), currentPos(0), abortDownload(false) {
        curl = curl_easy_init();
        if (!curl)
            throw std::runtime_error("Failed to initialize curl");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeData);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &abortDownload);

        downloadedData.resize(BUFFER_SIZE);
        bufferSize = 0;
        bufferPos = 0;
    }

    ~UrlDataSource() override {
        if (curl)
            curl_easy_cleanup(curl);
    }

    bool read(char* buffer, size_t size) override {
        while (bufferPos + size > bufferSize) {
            if (bufferPos >= bufferSize) {
                bufferSize = 0;
                bufferPos = 0;
            }

            if (bufferPos > 0 && bufferSize > bufferPos) {
                memmove(&downloadedData[0], &downloadedData[bufferPos], bufferSize - bufferPos);
                bufferSize -= bufferPos;
                bufferPos = 0;
            }

            writeData.buffer = &downloadedData[bufferSize];
            writeData.size = downloadedData.size() - bufferSize;
            writeData.pos = 0;
            writeData.abort_download = &abortDownload;

            std::string range = std::to_string(currentPos + bufferSize) + "-" +
                std::to_string(currentPos + bufferSize + CHUNK_SIZE - 1);
            curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                return false;
            }

            if (writeData.pos == 0) {
                _eof = true;
                return false;
            }
            bufferSize += writeData.pos;
        }

        // Specify the template type explicitly to avoid macro issues with std::min.
        size_t copySize = std::min<size_t>(size, bufferSize - bufferPos);
        memcpy(buffer, &downloadedData[bufferPos], copySize);
        bufferPos += copySize;
        currentPos += copySize;

        return copySize == size;
    }

    bool seek(size_t position) override {
        if (position >= currentPos - bufferPos && position < currentPos + (bufferSize - bufferPos)) {
            bufferPos = position - (currentPos - bufferPos);
            currentPos = position;
            return true;
        }
        bufferSize = 0;
        bufferPos = 0;
        currentPos = position;
        _eof = false;
        return true;
    }

    bool eof() const override {
        return _eof;
    }

    size_t tell() override {
        return currentPos;
    }

    void setAbortFlag() {
        abortDownload = true;
    }

private:
    static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        CurlBuffer* data = static_cast<CurlBuffer*>(userdata);
        if (*(data->abort_download))
            return 0;
        size_t bytes = size * nmemb;
        size_t available = data->size - data->pos;
        if (bytes > available)
            bytes = available;
        memcpy(data->buffer + data->pos, ptr, bytes);
        data->pos += bytes;
        return bytes;
    }

    static int ProgressCallback(void* clientp, curl_off_t, curl_off_t dlnow, curl_off_t, curl_off_t) {
        bool* abort_flag = static_cast<bool*>(clientp);
        return (*abort_flag) ? 1 : 0;
    }

    std::string url;
    CURL* curl;
    CurlBuffer writeData;
    std::vector<char> downloadedData;
    size_t bufferSize;
    size_t bufferPos;
    size_t currentPos;
    bool abortDownload;
    bool _eof = false;

    static constexpr size_t BUFFER_SIZE = 1024 * 1024;  // 1MB buffer
    static constexpr size_t CHUNK_SIZE = 256 * 1024;      // 256KB chunk size
};

class GGUFMetadataReader {
public:
    // GGUF metadata types
    enum class GGUFType : uint32_t {
        UINT8 = 0,
        INT8 = 1,
        UINT16 = 2,
        INT16 = 3,
        UINT32 = 4,
        INT32 = 5,
        FLOAT32 = 6,
        BOOL = 7,
        STRING = 8,
        ARRAY = 9,
        UINT64 = 10,
        INT64 = 11,
        FLOAT64 = 12,
        MAX_TYPE = 13
    };

    GGUFMetadataReader() {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    ~GGUFMetadataReader() {
        curl_global_cleanup();
    }

    bool isUrl(const std::string& path) {
        return path.substr(0, 7) == "http://" || path.substr(0, 8) == "https://";
    }

    std::optional<GGUFModelParams> readModelParams(const std::string& path, bool verbose = false) {
        std::unique_ptr<DataSource> source;
        try {
            if (isUrl(path)) {
                if (verbose)
                    std::cout << "Reading from URL: " << path << std::endl;
                source = std::make_unique<UrlDataSource>(path);
            }
            else {
                if (verbose)
                    std::cout << "Reading from file: " << path << std::endl;
                source = std::make_unique<FileDataSource>(path);
            }

            uint32_t magic;
            if (!source->read(reinterpret_cast<char*>(&magic), sizeof(magic)))
                throw std::runtime_error("Failed to read magic number");
            if (magic != 0x46554747) {
                std::cerr << "Invalid GGUF file format. Magic number: "
                    << std::hex << magic << std::dec << std::endl;
                return std::nullopt;
            }

            uint32_t version;
            if (!source->read(reinterpret_cast<char*>(&version), sizeof(version)))
                throw std::runtime_error("Failed to read version");
            if (version > 3) {
                std::cerr << "Unsupported GGUF version: " << version << std::endl;
                return std::nullopt;
            }
            if (verbose)
                std::cout << "GGUF version: " << version << std::endl;

            uint64_t tensorCount = 0;
            if (version >= 1) {
                if (!source->read(reinterpret_cast<char*>(&tensorCount), sizeof(tensorCount)))
                    throw std::runtime_error("Failed to read tensor count");
                if (verbose)
                    std::cout << "Tensor count: " << tensorCount << std::endl;
            }

            uint64_t metadataCount;
            if (!source->read(reinterpret_cast<char*>(&metadataCount), sizeof(metadataCount)))
                throw std::runtime_error("Failed to read metadata count");
            if (verbose)
                std::cout << "Metadata count: " << metadataCount << std::endl;

            const std::vector<std::string> suffixes = {
                ".attention.head_count",
                ".attention.head_count_kv",
                ".block_count",
                ".embedding_length"
            };

            GGUFModelParams params;
            std::unordered_map<std::string, bool> foundParams;
            std::vector<std::string> allKeys;

            for (uint64_t i = 0; i < metadataCount && !source->eof(); ++i) {
                std::string key;
                try {
                    key = readString(source.get());
                    allKeys.push_back(key);
                }
                catch (const std::exception& e) {
                    throw std::runtime_error(std::string("Failed to read key: ") + e.what());
                }

                uint32_t typeVal;
                if (!source->read(reinterpret_cast<char*>(&typeVal), sizeof(typeVal)))
                    throw std::runtime_error("Failed to read metadata type for key: " + key);
                if (typeVal >= static_cast<uint32_t>(GGUFType::MAX_TYPE))
                    throw std::runtime_error("Invalid metadata type: " + std::to_string(typeVal) + " for key: " + key);
                GGUFType type = static_cast<GGUFType>(typeVal);

                if (verbose)
                    std::cout << "Key: " << key << ", Type: " << static_cast<int>(type) << std::endl;

                bool keyMatched = false;
                std::string matchedSuffix;
                for (const auto& suffix : suffixes) {
                    if (endsWith(key, suffix)) {
                        keyMatched = true;
                        matchedSuffix = suffix;
                        break;
                    }
                }

                if (keyMatched) {
                    if (matchedSuffix == ".attention.head_count" && (type == GGUFType::UINT32 || type == GGUFType::INT32)) {
                        uint32_t value;
                        if (!source->read(reinterpret_cast<char*>(&value), sizeof(value)))
                            throw std::runtime_error("Failed to read attention_heads value");
                        params.attention_heads = value;
                        foundParams["attention_heads"] = true;
                        if (verbose)
                            std::cout << "  Found attention_heads: " << value << " (from key: " << key << ")" << std::endl;
                    }
                    else if (matchedSuffix == ".attention.head_count_kv" && (type == GGUFType::UINT32 || type == GGUFType::INT32)) {
                        uint32_t value;
                        if (!source->read(reinterpret_cast<char*>(&value), sizeof(value)))
                            throw std::runtime_error("Failed to read kv_heads value");
                        params.kv_heads = value;
                        foundParams["kv_heads"] = true;
                        if (verbose)
                            std::cout << "  Found kv_heads: " << value << " (from key: " << key << ")" << std::endl;
                    }
                    else if (matchedSuffix == ".block_count" && (type == GGUFType::UINT32 || type == GGUFType::INT32)) {
                        uint32_t value;
                        if (!source->read(reinterpret_cast<char*>(&value), sizeof(value)))
                            throw std::runtime_error("Failed to read hidden_layers value");
                        params.hidden_layers = value;
                        foundParams["hidden_layers"] = true;
                        if (verbose)
                            std::cout << "  Found hidden_layers: " << value << " (from key: " << key << ")" << std::endl;
                    }
                    else if (matchedSuffix == ".embedding_length") {
                        if (type == GGUFType::UINT64 || type == GGUFType::INT64) {
                            uint64_t value;
                            if (!source->read(reinterpret_cast<char*>(&value), sizeof(value)))
                                throw std::runtime_error("Failed to read hidden_size value (64-bit)");
                            params.hidden_size = value;
                            foundParams["hidden_size"] = true;
                            if (verbose)
                                std::cout << "  Found hidden_size: " << value << " (from key: " << key << ")" << std::endl;
                        }
                        else if (type == GGUFType::UINT32 || type == GGUFType::INT32) {
                            uint32_t value;
                            if (!source->read(reinterpret_cast<char*>(&value), sizeof(value)))
                                throw std::runtime_error("Failed to read hidden_size value (32-bit)");
                            params.hidden_size = value;
                            foundParams["hidden_size"] = true;
                            if (verbose)
                                std::cout << "  Found hidden_size: " << value << " (from key: " << key << ")" << std::endl;
                        }
                        else {
                            skipValue(source.get(), type);
                        }
                    }
                    else {
                        skipValue(source.get(), type);
                    }
                }
                else {
                    skipValue(source.get(), type);
                }

                if (foundParams["attention_heads"] &&
                    foundParams["hidden_layers"] &&
                    foundParams["hidden_size"] &&
                    (foundParams["kv_heads"] || foundParams["attention_heads"])) {
                    if (isUrl(path)) {
                        auto urlSource = dynamic_cast<UrlDataSource*>(source.get());
                        if (urlSource) {
                            urlSource->setAbortFlag();
                            if (verbose)
                                std::cout << "All required metadata found, aborting download" << std::endl;
                        }
                    }
                    break;
                }
            }

            if (!foundParams["kv_heads"] && foundParams["attention_heads"]) {
                params.kv_heads = params.attention_heads;
                foundParams["kv_heads"] = true;
                if (verbose)
                    std::cout << "  Using attention_heads as kv_heads: " << params.kv_heads << std::endl;
            }

            bool allFound = foundParams["attention_heads"] &&
                foundParams["hidden_layers"] &&
                foundParams["hidden_size"];

            if (!allFound) {
                std::cerr << "Failed to find all required model parameters:" << std::endl;
                if (!foundParams["attention_heads"]) std::cerr << "  Missing: attention_heads (suffix: .attention.head_count)" << std::endl;
                if (!foundParams["hidden_layers"]) std::cerr << "  Missing: hidden_layers (suffix: .block_count)" << std::endl;
                if (!foundParams["hidden_size"]) std::cerr << "  Missing: hidden_size (suffix: .embedding_length)" << std::endl;
                if (verbose) {
                    std::cerr << "All keys found:" << std::endl;
                    for (const auto& key : allKeys)
                        std::cerr << "  " << key << std::endl;
                }
                return std::nullopt;
            }

            return params;
        }
        catch (const std::exception& e) {
            std::cerr << "Error reading GGUF file/URL: " << e.what() << std::endl;
            return std::nullopt;
        }
    }

private:
    bool endsWith(const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() &&
            str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::string readString(DataSource* source) {
        uint64_t length;
        if (!source->read(reinterpret_cast<char*>(&length), sizeof(length)))
            throw std::runtime_error("Failed to read string length");
        if (length > 1024 * 1024)
            throw std::runtime_error("String too long: " + std::to_string(length));
        std::string str(length, '\0');
        if (length > 0)
            if (!source->read(&str[0], length))
                throw std::runtime_error("Failed to read string data");
        return str;
    }

    void skipArray(DataSource* source, GGUFType elemType) {
        uint64_t count;
        if (!source->read(reinterpret_cast<char*>(&count), sizeof(count)))
            throw std::runtime_error("Failed to read array count");
        if (count > 1000000)
            throw std::runtime_error("Array count too large: " + std::to_string(count));
        for (uint64_t i = 0; i < count; ++i)
            skipValue(source, elemType);
    }

    void skipValue(DataSource* source, GGUFType type) {
        switch (type) {
        case GGUFType::UINT8:
            source->seek(source->tell() + sizeof(uint8_t));
            break;
        case GGUFType::INT8:
            source->seek(source->tell() + sizeof(int8_t));
            break;
        case GGUFType::UINT16:
            source->seek(source->tell() + sizeof(uint16_t));
            break;
        case GGUFType::INT16:
            source->seek(source->tell() + sizeof(int16_t));
            break;
        case GGUFType::UINT32:
            source->seek(source->tell() + sizeof(uint32_t));
            break;
        case GGUFType::INT32:
            source->seek(source->tell() + sizeof(int32_t));
            break;
        case GGUFType::FLOAT32:
            source->seek(source->tell() + sizeof(float));
            break;
        case GGUFType::BOOL:
            source->seek(source->tell() + sizeof(uint8_t));
            break;
        case GGUFType::STRING: {
            uint64_t length;
            if (!source->read(reinterpret_cast<char*>(&length), sizeof(length)))
                throw std::runtime_error("Failed to read string length for skipping");
            if (length > 1024 * 1024)
                throw std::runtime_error("String too long: " + std::to_string(length));
            source->seek(source->tell() + length);
            break;
        }
        case GGUFType::ARRAY: {
            uint32_t elemTypeVal;
            if (!source->read(reinterpret_cast<char*>(&elemTypeVal), sizeof(elemTypeVal)))
                throw std::runtime_error("Failed to read array element type");
            if (elemTypeVal >= static_cast<uint32_t>(GGUFType::MAX_TYPE))
                throw std::runtime_error("Invalid array element type: " + std::to_string(elemTypeVal));
            GGUFType elemType = static_cast<GGUFType>(elemTypeVal);
            skipArray(source, elemType);
            break;
        }
        case GGUFType::UINT64:
            source->seek(source->tell() + sizeof(uint64_t));
            break;
        case GGUFType::INT64:
            source->seek(source->tell() + sizeof(int64_t));
            break;
        case GGUFType::FLOAT64:
            source->seek(source->tell() + sizeof(double));
            break;
        default:
            throw std::runtime_error("Unknown GGUF type: " + std::to_string(static_cast<int>(type)));
        }
    }
};

#endif // GGUF_READER_H