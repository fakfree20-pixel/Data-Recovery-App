#include <jni.h>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_set>
#include <iomanip>
#include <sstream>
#include <android/log.h>

#define LOG_TAG "NativeCarver"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct CarvedResult {
    std::string fileType;
    long offset;
    long size;
    std::string savedPath;
};

// Size and sample-based checksum for deduplication of duplicate fragments/thumbnails
unsigned long long calculateChecksum(const unsigned char* data, size_t size) {
    unsigned long long hash = 5381;
    hash = ((hash << 5) + hash) + size;
    size_t sampleSize = size < 128 ? size : 128;
    for (size_t i = 0; i < sampleSize; ++i) {
        hash = ((hash << 5) + hash) + data[i];
        hash = ((hash << 5) + hash) + data[size - 1 - i];
    }
    return hash;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_example_MainActivity_carveMediaFilesFromBinary(
        JNIEnv* env,
        jobject /* this */,
        jstring filePathStr,
        jstring outputDirStr) {

    const char* filePath = env->GetStringUTFChars(filePathStr, nullptr);
    const char* outputDir = env->GetStringUTFChars(outputDirStr, nullptr);

    if (!filePath || !outputDir) {
        if (filePath) env->ReleaseStringUTFChars(filePathStr, filePath);
        if (outputDir) env->ReleaseStringUTFChars(outputDirStr, outputDir);
        return nullptr;
    }

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOGE("Failed to open file for carving: %s", filePath);
        env->ReleaseStringUTFChars(filePathStr, filePath);
        env->ReleaseStringUTFChars(outputDirStr, outputDir);
        return nullptr;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    LOGI("Deep carving with unique indexing & deduplication from: %s (size: %ld bytes)", filePath, (long)fileSize);

    std::vector<unsigned char> data(fileSize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fileSize)) {
        LOGE("Failed to read file into buffer");
        file.close();
        env->ReleaseStringUTFChars(filePathStr, filePath);
        env->ReleaseStringUTFChars(outputDirStr, outputDir);
        return nullptr;
    }
    file.close();

    std::vector<CarvedResult> results;
    std::unordered_set<unsigned long long> seenChecksums;
    size_t i = 0;
    int fileCounter = 1;

    while (i < data.size()) {
        // 1. JPEG: 0xFF, 0xD8, 0xFF ... 0xFF, 0xD9
        if (i + 2 < data.size() && data[i] == 0xFF && data[i+1] == 0xD8 && (data[i+2] == 0xFF || data[i+2] == 0xE0)) {
            size_t startOffset = i;
            size_t endOffset = 0;
            bool foundEnd = false;

            for (size_t j = startOffset + 3; j + 1 < data.size(); ++j) {
                if (data[j] == 0xFF && data[j+1] == 0xD9) {
                    endOffset = j + 2;
                    foundEnd = true;
                    break;
                }
            }

            if (foundEnd && (endOffset > startOffset)) {
                size_t jpegSize = endOffset - startOffset;
                if (jpegSize >= 100 && jpegSize <= 50 * 1024 * 1024) {
                    unsigned long long checksum = calculateChecksum(data.data() + startOffset, jpegSize);
                    if (seenChecksums.find(checksum) == seenChecksums.end()) {
                        seenChecksums.insert(checksum);

                        std::string outFileName = std::string(outputDir) + "/carved_" + std::to_string(fileCounter++) + ".jpg";
                        std::ofstream outFile(outFileName, std::ios::binary);
                        if (outFile.is_open()) {
                            outFile.write(reinterpret_cast<char*>(data.data() + startOffset), jpegSize);
                            outFile.close();
                            results.push_back({"JPEG", (long)startOffset, (long)jpegSize, outFileName});
                            LOGI("Carved unique JPEG carved_%d.jpg (size: %zu)", fileCounter - 1, jpegSize);
                        }
                    }
                }
                i = endOffset;
                continue;
            }
        }

        // 2. MP4: ftyp box
        if (i + 7 < data.size() && data[i+4] == 'f' && data[i+5] == 't' && data[i+6] == 'y' && data[i+7] == 'p') {
            size_t startOffset = i;
            uint32_t boxSize = (data[i] << 24) | (data[i+1] << 16) | (data[i+2] << 8) | data[i+3];
            size_t mp4Size = (boxSize >= 8 && boxSize <= 150 * 1024 * 1024) ? boxSize : (8 * 1024 * 1024);
            if (startOffset + mp4Size > data.size()) {
                mp4Size = data.size() - startOffset;
            }

            if (mp4Size >= 1024) {
                unsigned long long checksum = calculateChecksum(data.data() + startOffset, mp4Size < 1024 ? mp4Size : 1024);
                if (seenChecksums.find(checksum) == seenChecksums.end()) {
                    seenChecksums.insert(checksum);

                    std::string outFileName = std::string(outputDir) + "/carved_" + std::to_string(fileCounter++) + ".mp4";
                    std::ofstream outFile(outFileName, std::ios::binary);
                    if (outFile.is_open()) {
                        outFile.write(reinterpret_cast<char*>(data.data() + startOffset), mp4Size);
                        outFile.close();
                        results.push_back({"MP4", (long)startOffset, (long)mp4Size, outFileName});
                        LOGI("Carved unique MP4 carved_%d.mp4 (size: %zu)", fileCounter - 1, mp4Size);
                    }
                }
            }
            i += (mp4Size > 0 ? mp4Size : 4096);
            continue;
        }

        // 3. MP3: ID3 tag ("ID3")
        if (i + 2 < data.size() && data[i] == 'I' && data[i+1] == 'D' && data[i+2] == '3') {
            size_t startOffset = i;
            size_t mp3Size = 4 * 1024 * 1024;
            if (startOffset + mp3Size > data.size()) {
                mp3Size = data.size() - startOffset;
            }

            if (mp3Size >= 512) {
                unsigned long long checksum = calculateChecksum(data.data() + startOffset, 512);
                if (seenChecksums.find(checksum) == seenChecksums.end()) {
                    seenChecksums.insert(checksum);

                    std::string outFileName = std::string(outputDir) + "/carved_" + std::to_string(fileCounter++) + ".mp3";
                    std::ofstream outFile(outFileName, std::ios::binary);
                    if (outFile.is_open()) {
                        outFile.write(reinterpret_cast<char*>(data.data() + startOffset), mp3Size);
                        outFile.close();
                        results.push_back({"MP3", (long)startOffset, (long)mp3Size, outFileName});
                        LOGI("Carved unique MP3 carved_%d.mp3 (size: %zu)", fileCounter - 1, mp3Size);
                    }
                }
            }
            i += 8192;
            continue;
        }

        ++i;
    }

    env->ReleaseStringUTFChars(filePathStr, filePath);
    env->ReleaseStringUTFChars(outputDirStr, outputDir);

    jclass resultClass = env->FindClass("java/lang/String");
    jobjectArray jResults = env->NewObjectArray(results.size(), resultClass, nullptr);

    for (size_t k = 0; k < results.size(); ++k) {
        std::string info = results[k].fileType + "|" + std::to_string(results[k].offset) + "|" + std::to_string(results[k].size) + "|" + results[k].savedPath;
        jstring jInfo = env->NewStringUTF(info.c_str());
        env->SetObjectArrayElement(jResults, k, jInfo);
        env->DeleteLocalRef(jInfo);
    }

    return jResults;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_example_MainActivity_getTotalCarvedCount(
        JNIEnv* env,
        jobject /* this */,
        jstring filePathStr) {
    return 0;
}
