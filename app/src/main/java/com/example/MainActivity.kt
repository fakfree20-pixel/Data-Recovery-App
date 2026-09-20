package com.example

import android.Manifest
import android.content.ContentUris
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.MediaStore
import android.provider.Settings
import android.view.View
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.util.Locale

class MainActivity : AppCompatActivity() {

    companion object {
        init {
            System.loadLibrary("datarecovery")
        }
    }

    external fun carveMediaFilesFromBinary(filePath: String, outputDir: String): Array<String>?
    external fun getTotalCarvedCount(filePath: String): Int

    private lateinit var btnScanStorage: Button
    private lateinit var progressBar: ProgressBar
    private lateinit var tvStatus: TextView
    private lateinit var tvEmptyState: TextView
    private lateinit var recyclerViewImages: RecyclerView

    private lateinit var adapter: RecoveredImagesAdapter
    private val recoveredList = mutableListOf<RecoveredImage>()

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (isGranted) {
            startStorageScan()
        } else {
            Toast.makeText(this, R.string.permission_required, Toast.LENGTH_LONG).show()
            tvStatus.text = getString(R.string.permission_required)
        }
    }

    private val manageStorageLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { _ ->
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Environment.isExternalStorageManager()) {
                startStorageScan()
            } else {
                Toast.makeText(this, "All Files Access permission required", Toast.LENGTH_LONG).show()
                tvStatus.text = "Permission denied"
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        btnScanStorage = findViewById(R.id.btnScanStorage)
        progressBar = findViewById(R.id.progressBar)
        tvStatus = findViewById(R.id.tvStatus)
        tvEmptyState = findViewById(R.id.tvEmptyState)
        recyclerViewImages = findViewById(R.id.recyclerViewImages)

        recyclerViewImages.layoutManager = LinearLayoutManager(this)
        adapter = RecoveredImagesAdapter(recoveredList) { image ->
            Toast.makeText(this, "Recovered: ${image.name}", Toast.LENGTH_SHORT).show()
        }
        recyclerViewImages.adapter = adapter

        btnScanStorage.setOnClickListener {
            checkAndRequestPermission()
        }
    }

    private fun checkAndRequestPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Environment.isExternalStorageManager()) {
                startStorageScan()
            } else {
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION).apply {
                        data = Uri.parse("package:$packageName")
                    }
                    manageStorageLauncher.launch(intent)
                } catch (e: Exception) {
                    val intent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                    manageStorageLauncher.launch(intent)
                }
            }
        } else {
            val permission = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                Manifest.permission.READ_MEDIA_IMAGES
            } else {
                Manifest.permission.READ_EXTERNAL_STORAGE
            }

            if (ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED) {
                startStorageScan()
            } else {
                permissionLauncher.launch(permission)
            }
        }
    }

    private fun startStorageScan() {
        btnScanStorage.isEnabled = false
        progressBar.visibility = View.VISIBLE
        tvStatus.text = getString(R.string.scanning)
        tvEmptyState.visibility = View.GONE

        lifecycleScope.launch(Dispatchers.IO) {
            val results = mutableListOf<RecoveredImage>()
            val seenPaths = mutableSetOf<String>()
            var totalCarvedCount = 0

            val carvedOutputDir = File(filesDir, "CarvedMedia").apply {
                if (!exists()) mkdirs()
            }

            // 1. Query MediaStore for media files
            try {
                val projection = arrayOf(
                    MediaStore.Images.Media._ID,
                    MediaStore.Images.Media.DISPLAY_NAME,
                    MediaStore.Images.Media.SIZE,
                    MediaStore.Images.Media.DATA,
                    MediaStore.Images.Media.DATE_MODIFIED,
                    MediaStore.Images.Media.MIME_TYPE
                )

                contentResolver.query(
                    MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
                    projection,
                    null,
                    null,
                    "${MediaStore.Images.Media.DATE_MODIFIED} DESC"
                )?.use { cursor ->
                    val idCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media._ID)
                    val nameCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DISPLAY_NAME)
                    val sizeCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.SIZE)
                    val dataCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATA)
                    val dateCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATE_MODIFIED)

                    while (cursor.moveToNext()) {
                        val id = cursor.getLong(idCol)
                        val name = cursor.getString(nameCol) ?: "unknown"
                        val size = cursor.getLong(sizeCol)
                        val path = cursor.getString(dataCol) ?: continue
                        val date = cursor.getLong(dateCol)
                        val uri = ContentUris.withAppendedId(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, id)

                        if (seenPaths.add(path)) {
                            results.add(RecoveredImage(id, name, size, path, uri, date))
                        }
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }

            // 2. Explicitly scan hidden directories (/DCIM/.thumbnails, WhatsApp, Telegram, caches)
            val externalDir = Environment.getExternalStorageDirectory()
            val hiddenDirsToScan = listOfNotNull(
                File(externalDir, "DCIM/.thumbnails"),
                File(externalDir, "Pictures/.thumbnails"),
                File(externalDir, "WhatsApp/Media/WhatsApp Images"),
                File(externalDir, "WhatsApp/Media/WhatsApp Video"),
                File(externalDir, "WhatsApp/Media/WhatsApp Audio"),
                File(externalDir, "Telegram/Telegram Images"),
                File(externalDir, "Telegram/Telegram Video"),
                File(externalDir, "Android/data/com.whatsapp/cache"),
                cacheDir,
                getExternalFilesDir(null)
            )

            for (dir in hiddenDirsToScan) {
                if (dir.exists() && dir.isDirectory) {
                    scanHiddenDirectory(dir, results, seenPaths, carvedOutputDir) { count ->
                        totalCarvedCount += count
                    }
                }
            }

            withContext(Dispatchers.Main) {
                progressBar.visibility = View.GONE
                btnScanStorage.isEnabled = true
                recoveredList.clear()
                recoveredList.addAll(results)
                adapter.updateData(recoveredList)

                if (recoveredList.isEmpty()) {
                    tvEmptyState.visibility = View.VISIBLE
                    tvStatus.text = "Scan complete. No media files or carved artifacts found."
                } else {
                    tvEmptyState.visibility = View.GONE
                    tvStatus.text = String.format(Locale.getDefault(), "Scan complete. Found %d items (%d deep-carved).", recoveredList.size, totalCarvedCount)
                }
            }
        }
    }

    private fun scanHiddenDirectory(
        dir: File,
        results: MutableList<RecoveredImage>,
        seenPaths: MutableSet<String>,
        carvedOutputDir: File,
        onCarved: (Int) -> Unit
    ) {
        val files = dir.listFiles() ?: return
        var carvedInDir = 0
        for (file in files) {
            try {
                if (file.isDirectory) {
                    scanHiddenDirectory(file, results, seenPaths, carvedOutputDir, onCarved)
                } else if (file.isFile) {
                    val path = file.absolutePath
                    if (!seenPaths.contains(path)) {
                        seenPaths.add(path)
                        val name = file.name
                        val lowerName = name.lowercase(Locale.getDefault())

                        // Deep carve using C++ native carver for JPEG, MP4, MP3
                        try {
                            val carvedArray = carveMediaFilesFromBinary(path, carvedOutputDir.absolutePath)
                            if (carvedArray != null && carvedArray.isNotEmpty()) {
                                carvedArray.forEach { carvedInfo ->
                                    val parts = carvedInfo.split("|")
                                    if (parts.size >= 4) {
                                        val type = parts[0]
                                        val savedPath = parts[3]
                                        val carvedFile = File(savedPath)
                                        if (carvedFile.exists() && carvedFile.length() > 0) {
                                            carvedInDir++
                                            results.add(
                                                RecoveredImage(
                                                    id = savedPath.hashCode().toLong(),
                                                    name = "[$type] ${carvedFile.name}",
                                                    size = carvedFile.length(),
                                                    path = savedPath,
                                                    uri = Uri.fromFile(carvedFile),
                                                    dateModified = file.lastModified() / 1000
                                                )
                                            )
                                        }
                                    }
                                }
                            } else {
                                // Add as regular file if match or thumbnail
                                if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg") || lowerName.endsWith(".mp4") || lowerName.endsWith(".mp3") || lowerName.contains("thumb")) {
                                    results.add(
                                        RecoveredImage(
                                            id = path.hashCode().toLong(),
                                            name = name,
                                            size = file.length(),
                                            path = path,
                                            uri = Uri.fromFile(file),
                                            dateModified = file.lastModified() / 1000
                                        )
                                    )
                                }
                            }
                        } catch (e: Exception) {
                            e.printStackTrace()
                        }
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
        if (carvedInDir > 0) {
            onCarved(carvedInDir)
        }
    }
}
