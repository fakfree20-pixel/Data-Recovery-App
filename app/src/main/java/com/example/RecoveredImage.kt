package com.example

import android.net.Uri

data class RecoveredImage(
    val id: Long,
    val name: String,
    val size: Long,
    val path: String,
    val uri: Uri,
    val dateModified: Long
)
