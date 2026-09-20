#include <jni.h>
#include <string>
#include <vector>
#include <fstream>
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

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_example_MainActivity_carveJpegsFromBinary(
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

    LOGI("Carving JPEGs from: %s (size: %ld bytes)", filePath, (long)fileSize);

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
    size_t i = 0;

    while (i + 1 < data.size()) {
        // Look for JPEG Start of Image (SOI): 0xFF, 0xD8 (Start magic bytes 0xFFD8 / 0xFFD8FF)
        if (data[i] == 0xFF && data[i+1] == 0xD8) {
            size_t startOffset = i;
            size_t endOffset = 0;
            bool foundEnd = false;

            // Search for End of Image (EOI): 0xFF, 0xD9
            for (size_t j = startOffset + 2; j + 1 < data.size(); ++j) {
                if (data[j] == 0xFF && data[j+1] == 0xD9) {
                    endOffset = j + 2; // Include 0xFF 0xD9
                    foundEnd = true;
                    break;
                }
            }

            if (foundEnd && (endOffset > startOffset)) {
                size_t jpegSize = endOffset - startOffset;
                // Validate reasonable JPEG size (100 bytes to 50MB)
                if (jpegSize >= 100 && jpegSize <= 50 * 1024 * 1024) {
                    std::string outFileName = std::string(outputDir) + "/carved_" + std::to_string(startOffset) + ".jpg";
                    
                    std::ofstream outFile(outFileName, std::ios::binary);
                    if (outFile.is_open()) {
                        outFile.write(reinterpret_cast<char*>(data.data() + startOffset), jpegSize);
                        outFile.close();

                        results.push_back({"JPEG", (long)startOffset, (long)jpegSize, outFileName});
                        LOGI("Successfully carved JPEG: offset=%zu, size=%zu -> %s", startOffset, jpegSize, outFileName.c_str());
                    }
                }
                i = endOffset; // Advance past this JPEG
                continue;
            }
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
