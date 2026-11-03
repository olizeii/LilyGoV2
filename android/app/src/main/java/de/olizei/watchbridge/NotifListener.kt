package de.olizei.watchbridge

import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

/**
 * Bekommt alle Benachrichtigungen des Handys (nach Freigabe in den Einstellungen)
 * und reicht sie an die [Bridge] weiter. Gleichzeitig Voraussetzung für die
 * Medien-Steuerung (MediaSessionManager).
 */
class NotifListener : NotificationListenerService() {

    private val app get() = WatchBridgeApp.instance

    override fun onListenerConnected() {
        instance = this
        app.bridge.media?.start()
    }

    override fun onListenerDisconnected() {
        if (instance === this) instance = null
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        app.bridge.onNotificationPosted(sbn, label(sbn.packageName))
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        app.bridge.onNotificationRemoved(sbn)
    }

    /** Nach dem Verbinden: aktuell sichtbare Benachrichtigungen erneut schicken (älteste zuerst). */
    fun resendActive() {
        val list = try {
            activeNotifications ?: return
        } catch (_: Exception) {
            return
        }
        list.sortedBy { it.postTime }.takeLast(15).forEach {
            app.bridge.onNotificationPosted(it, label(it.packageName), silent = true)
        }
    }

    fun dismiss(key: String) {
        try {
            cancelNotification(key)
        } catch (_: Exception) {
        }
    }

    private fun label(pkg: String): String = try {
        packageManager.getApplicationLabel(packageManager.getApplicationInfo(pkg, 0)).toString()
    } catch (_: Exception) {
        pkg
    }

    companion object {
        @Volatile var instance: NotifListener? = null
    }
}
