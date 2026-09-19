package com.example

import android.Manifest
import android.content.ContentUris
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.MediaStore
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

    private fun startStorageScan() {
        btnScanStorage.isEnabled = false
        progressBar.visibility = View.VISIBLE
        tvStatus.text = getString(R.string.scanning)
        tvEmptyState.visibility = View.GONE

        lifecycleScope.launch(Dispatchers.IO) {
            val results = mutableListOf<RecoveredImage>()

            // 1. Query MediaStore for JPEG images
            try {
                val projection = arrayOf(
                    MediaStore.Images.Media._ID,
                    MediaStore.Images.Media.DISPLAY_NAME,
                    MediaStore.Images.Media.SIZE,
                    MediaStore.Images.Media.DATA,
                    MediaStore.Images.Media.DATE_MODIFIED,
                    MediaStore.Images.Media.MIME_TYPE
                )

                val selection = "${MediaStore.Images.Media.MIME_TYPE} = ? OR ${MediaStore.Images.Media.MIME_TYPE} = ?"
                val selectionArgs = arrayOf("image/jpeg", "image/jpg")
                val sortOrder = "${MediaStore.Images.Media.DATE_MODIFIED} DESC"

                contentResolver.query(
                    MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
                    projection,
                    selection,
                    selectionArgs,
                    sortOrder
                )?.use { cursor ->
                    val idCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media._ID)
                    val nameCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DISPLAY_NAME)
                    val sizeCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.SIZE)
                    val dataCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATA)
                    val dateCol = cursor.getColumnIndexOrThrow(MediaStore.Images.Media.DATE_MODIFIED)

                    while (cursor.moveToNext()) {
                        val id = cursor.getLong(idCol)
                        val name = cursor.getString(nameCol) ?: "unknown.jpg"
                        val size = cursor.getLong(sizeCol)
                        val path = cursor.getString(dataCol) ?: "/storage/emulated/0/Pictures/$name"
                        val date = cursor.getLong(dateCol)
                        val uri = ContentUris.withAppendedId(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, id)

                        results.add(RecoveredImage(id, name, size, path, uri, date))
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }

            // 2. Also deep scan cache / pictures directories for any JPEG files or signatures
            try {
                val dirsToScan = listOf(
                    getExternalFilesDir(null),
                    cacheDir,
                    getExternalCacheDir()
                )

                for (dir in dirsToScan) {
                    dir?.let { scanDirectoryForJpegs(it, results) }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }

            withContext(Dispatchers.Main) {
                progressBar.visibility = View.GONE
                btnScanStorage.isEnabled = true
                recoveredList.clear()
                recoveredList.addAll(results)
                adapter.updateData(recoveredList)

                if (recoveredList.isEmpty()) {
                    tvEmptyState.visibility = View.VISIBLE
                    tvStatus.text = "Scan complete. No JPEG images found."
                } else {
                    tvEmptyState.visibility = View.GONE
                    tvStatus.text = String.format(Locale.getDefault(), "Scan complete. Found %d recovered JPEG(s).", recoveredList.size)
                }
            }
        }
    }

    private fun scanDirectoryForJpegs(dir: File, results: MutableList<RecoveredImage>) {
        val files = dir.listFiles() ?: return
        for (file in files) {
            if (file.isDirectory) {
                scanDirectoryForJpegs(file, results)
            } else if (file.isFile) {
                val name = file.name.lowercase(Locale.getDefault())
                if (name.endsWith(".jpg") || name.endsWith(".jpeg") || hasJpegHeader(file)) {
                    val uri = Uri.fromFile(file)
                    results.add(
                        RecoveredImage(
                            id = file.absolutePath.hashCode().toLong(),
                            name = file.name,
                            size = file.length(),
                            path = file.absolutePath,
                            uri = uri,
                            dateModified = file.lastModified() / 1000
                        )
                    )
                }
            }
        }
    }

    private fun hasJpegHeader(file: File): Boolean {
        if (file.length() < 3) return false
        return try {
            file.inputStream().use { input ->
                val b1 = input.read()
                val b2 = input.read()
                val b3 = input.read()
                b1 == 0xFF && b2 == 0xD8 && b3 == 0xFF
            }
        } catch (e: Exception) {
            false
        }
    }
}
