import QtQuick

QtObject {
    id: root

    property var host: null
    property var window: null

    readonly property var menuGroups: [
        "File", "Edit", "View", "Document", "Production", "Preflight", "Help", "Advanced"
    ]

    function descriptorsForGroup(groupName) {
        if (!host) {
            return []
        }

        const descriptors = host.commandDescriptors()
        const entries = []
        for (let index = 0; index < descriptors.length; ++index) {
            const entry = descriptors[index]
            const disposition = entry.disposition || ""
            if (disposition === "HIDE" || disposition === "STOP-SHIPPING") {
                continue
            }
            if (groupName === "Advanced") {
                if (disposition !== "ADVANCED") {
                    continue
                }
                if (!host.allowDeveloperDiagnostics) {
                    continue
                }
            } else if (entry.menuGroup !== groupName) {
                continue
            } else if (disposition === "ADVANCED" && !host.allowDeveloperDiagnostics) {
                continue
            }
            entries.push(entry)
        }
        return entries
    }

    function labelForEntry(entry) {
        if (!entry || !entry.labelKey) {
            return entry && entry.id ? entry.id : ""
        }
        const key = entry.labelKey
        const suffix = key.startsWith("command.") ? key.slice("command.".length) : key
        const base = suffix.endsWith(".label") ? suffix.slice(0, -".label".length) : suffix
        const words = base.replace(/^action/, "").replace(/_/g, " ").replace(/([a-z])([A-Z])/g, "$1 $2")
        if (words.length === 0) {
            return entry.id
        }
        return words.charAt(0).toUpperCase() + words.slice(1)
    }

    function commandEnabled(commandId) {
        return host ? host.isCommandEnabled(commandId) : false
    }

    function invoke(commandId) {
        if (!host) {
            return
        }
        if (commandId === "actionOpen" && window && window.openDialog) {
            if (host.focusRestoration) {
                host.focusRestoration.remember(window.activeFocusItem)
            }
            window.openDialog.open()
            return
        }
        if (commandId === "actionSave_As" && window && window.saveAsDialog) {
            if (host.focusRestoration) {
                host.focusRestoration.remember(window.activeFocusItem)
            }
            window.saveAsDialog.open()
            return
        }
        host.invokeCommand(commandId)
    }
}
