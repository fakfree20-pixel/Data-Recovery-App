#include <jni.h>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_set>
#include <algorithm>
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

    LOGI("Deep carving with 64KB chunks & overlap window from: %s (size: %ld bytes)", filePath, (long)fileSize);

    std::vector<CarvedResult> results;
    std::unordered_set<unsigned long long> seenChecksums;

    const size_t CHUNK_SIZE = 65536; // 64KB
    const size_t OVERLAP = 4096;     // 4KB overlap window to catch split headers
    std::vector<unsigned char> chunk(CHUNK_SIZE + OVERLAP);

    long long fileOffset = 0;
    int imgCounter = 1;
    int vidCounter = 1;
    int audCounter = 1;

    while (fileOffset < fileSize) {
        file.seekg(fileOffset, std::ios::beg);
        size_t bytesToRead = CHUNK_SIZE;
        if (fileOffset + CHUNK_SIZE > (size_t)fileSize) {
            bytesToRead = fileSize - fileOffset;
        }

        file.read(reinterpret_cast<char*>(chunk.data()), bytesToRead);
        std::streamsize bytesRead = file.gcount();
        if (bytesRead <= 0) break;

        size_t scanLimit = bytesRead;
        size_t i = 0;

        while (i < scanLimit) {
            size_t absoluteOffset = fileOffset + i;

            // 1. JPEG: 0xFF, 0xD8, 0xFF ... 0xFF, 0xD9
            if (i + 2 < (size_t)bytesRead && chunk[i] == 0xFF && chunk[i+1] == 0xD8 && (chunk[i+2] == 0xFF || chunk[i+2] == 0xE0)) {
                size_t startIdx = i;
                size_t endIdx = 0;
                bool foundEnd = false;

                for (size_t j = startIdx + 3; j + 1 < (size_t)bytesRead; ++j) {
                    if (chunk[j] == 0xFF && chunk[j+1] == 0xD9) {
                        endIdx = j + 2;
                        foundEnd = true;
                        break;
                    }
                }

                if (foundEnd && endIdx > startIdx) {
                    size_t jpegSize = endIdx - startIdx;
                    if (jpegSize >= 100 && jpegSize <= 50 * 1024 * 1024) {
                        unsigned long long checksum = calculateChecksum(chunk.data() + startIdx, jpegSize);
                        if (seenChecksums.find(checksum) == seenChecksums.end()) {
                            seenChecksums.insert(checksum);
                            std::string outFileName = std::string(outputDir) + "/carved_image_" + std::to_string(imgCounter++) + ".jpg";
                            std::ofstream outFile(outFileName, std::ios::binary);
                            if (outFile.is_open()) {
                                outFile.write(reinterpret_cast<char*>(chunk.data() + startIdx), jpegSize);
                                outFile.close();
                                CarvedResult res;
                                res.fileType = "JPEG";
                                res.offset = static_cast<long>(absoluteOffset);
                                res.size = static_cast<long>(jpegSize);
                                res.savedPath = outFileName;
                                results.push_back(res);
                                LOGI("Carved JPEG: carved_image_%d.jpg", imgCounter - 1);
                            }
                        }
                    }
                    i = endIdx;
                    continue;
                }
            }

            // 2. MP4: ftyp box
            if (i + 7 < (size_t)bytesRead && chunk[i+4] == 'f' && chunk[i+5] == 't' && chunk[i+6] == 'y' && chunk[i+7] == 'p') {
                size_t startIdx = i;
                uint32_t boxSize = (chunk[i] << 24) | (chunk[i+1] << 16) | (chunk[i+2] << 8) | chunk[i+3];
                size_t mp4Size = (boxSize >= 8 && boxSize <= 200 * 1024 * 1024) ? boxSize : (10 * 1024 * 1024);
                if (absoluteOffset + mp4Size > (size_t)fileSize) {
                    mp4Size = fileSize - absoluteOffset;
                }

                if (mp4Size >= 1024) {
                    unsigned long long checksum = calculateChecksum(chunk.data() + startIdx, mp4Size < 1024 ? mp4Size : 1024);
                    if (seenChecksums.find(checksum) == seenChecksums.end()) {
                        seenChecksums.insert(checksum);
                        std::string outFileName = std::string(outputDir) + "/carved_video_" + std::to_string(vidCounter++) + ".mp4";
                        std::ofstream outFile(outFileName, std::ios::binary);
                        if (outFile.is_open()) {
                            outFile.write(reinterpret_cast<char*>(chunk.data() + startIdx), mp4Size);
                            outFile.close();
                            CarvedResult res;
                            res.fileType = "MP4";
                            res.offset = static_cast<long>(absoluteOffset);
                            res.size = static_cast<long>(mp4Size);
                            res.savedPath = outFileName;
                            results.push_back(res);
                            LOGI("Carved MP4 Video: carved_video_%d.mp4", vidCounter - 1);
                        }
                    }
                }
                i += (mp4Size > 0 && mp4Size < CHUNK_SIZE ? mp4Size : 4096);
                continue;
            }

            // 3. MKV (Matroska): 0x1A, 0x45, 0xDF, 0xA3
            if (i + 3 < (size_t)bytesRead && chunk[i] == 0x1A && chunk[i+1] == 0x45 && chunk[i+2] == 0xDF && chunk[i+3] == 0xA3) {
                size_t startIdx = i;
                size_t mkvSize = 15 * 1024 * 1024;
                if (absoluteOffset + mkvSize > (size_t)fileSize) {
                    mkvSize = fileSize - absoluteOffset;
                }

                unsigned long long checksum = calculateChecksum(chunk.data() + startIdx, 1024);
                if (seenChecksums.find(checksum) == seenChecksums.end()) {
                    seenChecksums.insert(checksum);
                    std::string outFileName = std::string(outputDir) + "/carved_video_" + std::to_string(vidCounter++) + ".mkv";
                    std::ofstream outFile(outFileName, std::ios::binary);
                    if (outFile.is_open()) {
                        outFile.write(reinterpret_cast<char*>(chunk.data() + startIdx), mkvSize);
                        outFile.close();
                        CarvedResult res;
                        res.fileType = "MKV";
                        res.offset = static_cast<long>(absoluteOffset);
                        res.size = static_cast<long>(mkvSize);
                        res.savedPath = outFileName;
                        results.push_back(res);
                        LOGI("Carved MKV Video: carved_video_%d.mkv", vidCounter - 1);
                    }
                }
                i += 8192;
                continue;
            }

            // 4. AVI: RIFF ... AVI
            if (i + 11 < (size_t)bytesRead && chunk[i] == 'R' && chunk[i+1] == 'I' && chunk[i+2] == 'F' && chunk[i+3] == 'F' &&
                chunk[i+8] == 'A' && chunk[i+9] == 'V' && chunk[i+10] == 'I' && chunk[i+11] == ' ') {
                size_t startIdx = i;
                uint32_t riffSize = (chunk[i+7] << 24) | (chunk[i+6] << 16) | (chunk[i+5] << 8) | chunk[i+4];
                size_t aviSize = (riffSize > 0 && riffSize <= 200 * 1024 * 1024) ? (riffSize + 8) : (10 * 1024 * 1024);
                if (absoluteOffset + aviSize > (size_t)fileSize) {
                    aviSize = fileSize - absoluteOffset;
                }

                unsigned long long checksum = calculateChecksum(chunk.data() + startIdx, 1024);
                if (seenChecksums.find(checksum) == seenChecksums.end()) {
                    seenChecksums.insert(checksum);
                    std::string outFileName = std::string(outputDir) + "/carved_video_" + std::to_string(vidCounter++) + ".avi";
                    std::ofstream outFile(outFileName, std::ios::binary);
                    if (outFile.is_open()) {
                        outFile.write(reinterpret_cast<char*>(chunk.data() + startIdx), aviSize);
                        outFile.close();
                        CarvedResult res;
                        res.fileType = "AVI";
                        res.offset = static_cast<long>(absoluteOffset);
                        res.size = static_cast<long>(aviSize);
                        res.savedPath = outFileName;
                        results.push_back(res);
                        LOGI("Carved AVI Video: carved_video_%d.avi", vidCounter - 1);
                    }
                }
                i += 8192;
                continue;
            }

            // 5. MP3 (ID3 tag or Frame Sync 0xFFFB / 0xFFFA)
            if ((i + 2 < (size_t)bytesRead && chunk[i] == 'I' && chunk[i+1] == 'D' && chunk[i+2] == '3') ||
                (i + 1 < (size_t)bytesRead && chunk[i] == 0xFF && (chunk[i+1] == 0xFB || chunk[i+1] == 0xFA || chunk[i+1] == 0xF3))) {
                size_t startIdx = i;
                size_t mp3Size = 5 * 1024 * 1024;
                if (absoluteOffset + mp3Size > (size_t)fileSize) {
                    mp3Size = fileSize - absoluteOffset;
                }

                unsigned long long checksum = calculateChecksum(chunk.data() + startIdx, 512);
                if (seenChecksums.find(checksum) == seenChecksums.end()) {
                    seenChecksums.insert(checksum);
                    std::string outFileName = std::string(outputDir) + "/carved_audio_" + std::to_string(audCounter++) + ".mp3";
                    std::ofstream outFile(outFileName, std::ios::binary);
                    if (outFile.is_open()) {
                        outFile.write(reinterpret_cast<char*>(chunk.data() + startIdx), mp3Size);
                        outFile.close();
                        CarvedResult res;
                        res.fileType = "MP3";
                        res.offset = static_cast<long>(absoluteOffset);
                        res.size = static_cast<long>(mp3Size);
                        res.savedPath = outFileName;
                        results.push_back(res);
                        LOGI("Carved MP3 Audio: carved_audio_%d.mp3", audCounter - 1);
                    }
                }
                i += 4096;
                continue;
            }

            // 6. AAC (ADTS header: 0xFFF1 or 0xFFF9)
            if (i + 1 < (size_t)bytesRead && chunk[i] == 0xFF && (chunk[i+1] == 0xF1 || chunk[i+1] == 0xF9)) {
                size_t startIdx = i;
                size_t aacSize = 3 * 1024 * 1024;
                if (absoluteOffset + aacSize > (size_t)fileSize) {
                    aacSize = fileSize - absoluteOffset;
                }

                unsigned long long checksum = calculateChecksum(chunk.data() + startIdx, 512);
                if (seenChecksums.find(checksum) == seenChecksums.end()) {
                    seenChecksums.insert(checksum);
                    std::string outFileName = std::string(outputDir) + "/carved_audio_" + std::to_string(audCounter++) + ".aac";
                    std::ofstream outFile(outFileName, std::ios::binary);
                    if (outFile.is_open()) {
                        outFile.write(reinterpret_cast<char*>(chunk.data() + startIdx), aacSize);
                        outFile.close();
                        CarvedResult res;
                        res.fileType = "AAC";
                        res.offset = static_cast<long>(absoluteOffset);
                        res.size = static_cast<long>(aacSize);
                        res.savedPath = outFileName;
                        results.push_back(res);
                        LOGI("Carved AAC Audio: carved_audio_%d.aac", audCounter - 1);
                    }
                }
                i += 4096;
                continue;
            }

            ++i;
        }

        fileOffset += (CHUNK_SIZE - OVERLAP);
        if (fileOffset >= fileSize) break;
    }

    file.close();
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
