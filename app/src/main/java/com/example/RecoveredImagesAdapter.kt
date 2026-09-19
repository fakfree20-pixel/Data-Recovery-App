package com.example

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.ImageView
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import java.util.Locale

class RecoveredImagesAdapter(
    private var images: List<RecoveredImage>,
    private val onRecoverClick: (RecoveredImage) -> Unit
) : RecyclerView.Adapter<RecoveredImagesAdapter.ViewHolder>() {

    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        val ivThumbnail: ImageView = view.findViewById(R.id.ivThumbnail)
        val tvFileName: TextView = view.findViewById(R.id.tvFileName)
        val tvFileSize: TextView = view.findViewById(R.id.tvFileSize)
        val tvFilePath: TextView = view.findViewById(R.id.tvFilePath)
        val btnRecoverItem: Button = view.findViewById(R.id.btnRecoverItem)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_recovered_image, parent, false)
        return ViewHolder(view)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val item = images[position]
        holder.tvFileName.text = item.name
        holder.tvFileSize.text = "Size: ${formatSize(item.size)}"
        holder.tvFilePath.text = item.path

        try {
            holder.ivThumbnail.setImageURI(item.uri)
        } catch (e: Exception) {
            holder.ivThumbnail.setImageResource(android.R.drawable.ic_menu_gallery)
        }

        holder.btnRecoverItem.setOnClickListener {
            onRecoverClick(item)
        }
    }

    override fun getItemCount() = images.size

    fun updateData(newImages: List<RecoveredImage>) {
        images = newImages
        notifyDataSetChanged()
    }

    private fun formatSize(bytes: Long): String {
        if (bytes < 1024) return "$bytes B"
        val kb = bytes / 1024
        if (kb < 1024) return "$kb KB"
        val mb = kb / 1024
        return String.format(Locale.getDefault(), "%.1f MB", mb.toFloat())
    }
}
