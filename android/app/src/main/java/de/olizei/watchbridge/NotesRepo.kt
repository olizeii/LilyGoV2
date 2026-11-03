package de.olizei.watchbridge

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID

/** Notizen der Begleit-App (JSON-Datei). Nur Notizen mit [Note.onWatch] gehen auf die Uhr. */
class NotesRepo(ctx: Context) {

    data class Note(
        val id: String = UUID.randomUUID().toString().take(8),
        val title: String = "",
        val body: String = "",
        val onWatch: Boolean = true,
        val updated: Long = System.currentTimeMillis(),
    )

    private val file = File(ctx.filesDir, "notes.json")
    private val _notes = MutableStateFlow(load())
    val notes: StateFlow<List<Note>> = _notes

    /** Wird nach jeder Änderung aufgerufen (-> Sync zur Uhr). */
    var onChanged: (() -> Unit)? = null

    fun get(id: String): Note? = _notes.value.firstOrNull { it.id == id }

    fun upsert(note: Note) {
        val n = note.copy(updated = System.currentTimeMillis())
        val list = _notes.value.filterNot { it.id == n.id }
        commit((listOf(n) + list).sortedByDescending { it.updated })
    }

    fun delete(id: String) = commit(_notes.value.filterNot { it.id == id })

    fun setOnWatch(id: String, on: Boolean) {
        commit(_notes.value.map { if (it.id == id) it.copy(onWatch = on) else it })
    }

    /** Geteilter Text: erste Zeile wird Titel, Rest Inhalt. */
    fun addShared(subject: String?, text: String): Note {
        val t = text.trim()
        val title = subject?.takeIf { it.isNotBlank() } ?: t.lineSequence().first().take(40)
        val body = if (subject.isNullOrBlank()) t.substringAfter('\n', "").trim().ifEmpty { t } else t
        val n = Note(title = title.trim(), body = body)
        upsert(n)
        return n
    }

    private fun commit(list: List<Note>) {
        _notes.value = list
        save(list)
        onChanged?.invoke()
    }

    private fun load(): List<Note> = try {
        val arr = JSONArray(file.readText())
        (0 until arr.length()).map {
            val o = arr.getJSONObject(it)
            Note(
                id = o.getString("id"),
                title = o.optString("title"),
                body = o.optString("body"),
                onWatch = o.optBoolean("onWatch", true),
                updated = o.optLong("updated"),
            )
        }
    } catch (_: Exception) {
        emptyList()
    }

    private fun save(list: List<Note>) {
        val arr = JSONArray()
        list.forEach {
            arr.put(
                JSONObject().put("id", it.id).put("title", it.title).put("body", it.body)
                    .put("onWatch", it.onWatch).put("updated", it.updated),
            )
        }
        val tmp = File(file.parentFile, "notes.json.tmp")
        tmp.writeText(arr.toString())
        tmp.renameTo(file)
    }
}
