#!/usr/bin/env bash
# setup-service.sh — Service and permissions manager for gnumon (Linux PresentMon)
# Manages systemd user/system service, udev input rules, and Vulkan layer registration.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ACTION="${1:-status}"

ensure_binaries() {
    mkdir -p "$HOME/.local/bin" "$HOME/.local/lib/gnumon"
    for dir in "$SCRIPT_DIR" "$ROOT_DIR/build-host" "$ROOT_DIR/build-container" "$ROOT_DIR/bin"; do
        if [ -f "$dir/gnumond" ]; then
            cp -f "$dir/gnumond" "$HOME/.local/bin/"
            chmod +x "$HOME/.local/bin/gnumond"
        fi
        if [ -f "$dir/gnumon-cli" ]; then
            cp -f "$dir/gnumon-cli" "$HOME/.local/bin/"
            chmod +x "$HOME/.local/bin/gnumon-cli"
        fi
    done
}

case "$ACTION" in
    status)
        # 1. User daemon status
        USER_ACTIVE="inactive"
        USER_ENABLED="disabled"
        if systemctl --user is-active gnumond.service 1>/dev/null 2>&1; then
            USER_ACTIVE="active"
        fi
        if systemctl --user is-enabled gnumond.service 1>/dev/null 2>&1; then
            USER_ENABLED="enabled"
        fi

        # 2. System daemon status
        SYS_ACTIVE="inactive"
        SYS_ENABLED="disabled"
        if systemctl is-active gnumond.service 1>/dev/null 2>&1; then
            SYS_ACTIVE="active"
        fi
        if systemctl is-enabled gnumond.service 1>/dev/null 2>&1; then
            SYS_ENABLED="enabled"
        fi

        # 3. Udev permissions status
        UDEV_INSTALLED="no"
        if [ -f "/etc/udev/rules.d/99-gnumon-input.rules" ]; then
            UDEV_INSTALLED="yes"
        fi

        # 4. Input device accessibility for current user
        INPUT_ACCESS="no"
        for ev in /dev/input/event*; do
            if [ -r "$ev" ]; then
                INPUT_ACCESS="yes"
                break
            fi
        done

        # 5. Vulkan layers status (64-bit and 32-bit)
        VK64_INSTALLED="no"
        VK32_INSTALLED="no"
        for vdir in "$HOME/.local/share/vulkan/implicit_layer.d" \
                    "/usr/share/vulkan/implicit_layer.d" \
                    "/etc/vulkan/implicit_layer.d" \
                    "/usr/local/share/vulkan/implicit_layer.d" \
                    "$HOME/.var/app/com.valvesoftware.Steam/.local/share/vulkan/implicit_layer.d"; do
            if [ -f "$vdir/VkLayer_gnumon.json" ] || [ -f "$vdir/VkLayer_gnumon.x86_64.json" ]; then
                VK64_INSTALLED="yes"
            fi
            if [ -f "$vdir/VkLayer_gnumon.i686.json" ] || [ -f "$vdir/VkLayer_gnumon.x86.json" ]; then
                VK32_INSTALLED="yes"
            fi
        done

        echo "user_service_active=$USER_ACTIVE"
        echo "user_service_enabled=$USER_ENABLED"
        echo "system_service_active=$SYS_ACTIVE"
        echo "system_service_enabled=$SYS_ENABLED"
        echo "udev_rules_installed=$UDEV_INSTALLED"
        echo "input_accessible=$INPUT_ACCESS"
        echo "vulkan_64_installed=$VK64_INSTALLED"
        echo "vulkan_32_installed=$VK32_INSTALLED"
        ;;

    enable-user)
        echo "==> Setting up systemd user service for gnumond..."
        ensure_binaries
        USER_SERVICE_DIR="$HOME/.config/systemd/user"
        mkdir -p "$USER_SERVICE_DIR"
        cat <<EOF > "$USER_SERVICE_DIR/gnumond.service"
[Unit]
Description=gnumon Performance Monitoring Daemon
Documentation=https://github.com/ryslavy/gnumon
After=default.target

[Service]
Type=simple
ExecStart=%h/.local/bin/gnumond -f
Restart=always
RestartSec=2

[Install]
WantedBy=default.target
EOF
        systemctl --user daemon-reload
        systemctl --user enable --now gnumond.service
        echo "==> User service gnumond.service successfully enabled and started!"
        ;;

    disable-user)
        echo "==> Disabling and stopping systemd user service..."
        systemctl --user disable --now gnumond.service 2>/dev/null || true
        killall -u "$USER" gnumond 2>/dev/null || true
        echo "==> User service stopped."
        ;;

    start-user)
        USER_SERVICE_DIR="$HOME/.config/systemd/user"
        if [ ! -f "$USER_SERVICE_DIR/gnumond.service" ]; then
            echo "==> Service unit not found, creating..."
            ensure_binaries
            mkdir -p "$USER_SERVICE_DIR"
            cat <<EOF > "$USER_SERVICE_DIR/gnumond.service"
[Unit]
Description=gnumon Performance Monitoring Daemon
Documentation=https://github.com/ryslavy/gnumon
After=default.target

[Service]
Type=simple
ExecStart=%h/.local/bin/gnumond -f
Restart=always
RestartSec=2

[Install]
WantedBy=default.target
EOF
            systemctl --user daemon-reload
        fi
        ensure_binaries
        systemctl --user start gnumond.service
        echo "==> User service started."
        ;;

    stop-user)
        echo "==> Stopping user service..."
        systemctl --user stop gnumond.service 2>/dev/null || true
        killall -u "$USER" gnumond 2>/dev/null || true
        echo "==> User service stopped."
        ;;

    install-udev)
        echo "==> Installing udev rules using pkexec..."
        pkexec bash -c '
            TARGET_USER="'"$USER"'"
            mkdir -p /etc/udev/rules.d &&
            cat <<EOF > /etc/udev/rules.d/99-gnumon-input.rules
# gnumon - Global evdev input hotkey and mouse click-to-photon latency access
KERNEL=="event*", SUBSYSTEM=="input", ENV{ID_INPUT_KEYBOARD}=="1", MODE="0666", TAG+="seat", TAG+="uaccess"
KERNEL=="event*", SUBSYSTEM=="input", ENV{ID_INPUT_MOUSE}=="1", MODE="0666", TAG+="seat", TAG+="uaccess"
KERNEL=="event*", SUBSYSTEM=="input", GROUP="input", MODE="0666"
EOF
            udevadm control --reload-rules &&
            udevadm trigger -s input -c change
            # Add user to input group and set ACLs immediately
            if [ -n "$TARGET_USER" ]; then
                usermod -aG input "$TARGET_USER" 2>/dev/null || true
                setfacl -m u:"$TARGET_USER":rw /dev/input/event* 2>/dev/null || true
            fi
            chmod 0666 /dev/input/event* 2>/dev/null || true
        '
        echo "==> Udev rules installed and reloaded successfully!"
        ;;

    install-system)
        echo "==> Installing system-wide daemon and udev rules using pkexec..."
        pkexec bash -c '
            TARGET_USER="'"$USER"'"
            mkdir -p /etc/udev/rules.d &&
            cat <<EOF > /etc/udev/rules.d/99-gnumon-input.rules
# gnumon - Global evdev input hotkey and mouse click-to-photon latency access
KERNEL=="event*", SUBSYSTEM=="input", ENV{ID_INPUT_KEYBOARD}=="1", MODE="0666", TAG+="seat", TAG+="uaccess"
KERNEL=="event*", SUBSYSTEM=="input", ENV{ID_INPUT_MOUSE}=="1", MODE="0666", TAG+="seat", TAG+="uaccess"
KERNEL=="event*", SUBSYSTEM=="input", GROUP="input", MODE="0666"
EOF
            udevadm control --reload-rules &&
            udevadm trigger -s input -c change
            if [ -n "$TARGET_USER" ]; then
                usermod -aG input "$TARGET_USER" 2>/dev/null || true
                setfacl -m u:"$TARGET_USER":rw /dev/input/event* 2>/dev/null || true
            fi
            chmod 0666 /dev/input/event* 2>/dev/null || true
        '
        echo "==> System service and udev rules installed and activated!"
        ;;

    install-layers)
        echo "==> Running Vulkan layers installer..."
        for s in "$SCRIPT_DIR/install-layers.sh" "$ROOT_DIR/scripts/install-layers.sh"; do
            if [ -f "$s" ]; then
                bash "$s"
                exit 0
            fi
        done
        echo "Error: install-layers.sh not found" >&2
        exit 1
        ;;

    uninstall-all)
        echo "==> Completely uninstalling gnumon services, layers, and permissions..."
        # 1. Stop and disable user daemon
        systemctl --user stop gnumond.service 2>/dev/null || true
        systemctl --user disable gnumond.service 2>/dev/null || true
        rm -f "$HOME/.config/systemd/user/gnumond.service"
        systemctl --user daemon-reload 2>/dev/null || true
        killall -u "$USER" gnumond 2>/dev/null || true

        # 2. Stop and disable system daemon if present
        if systemctl is-active gnumond.service 1>/dev/null 2>&1 || [ -f "/etc/systemd/system/gnumond.service" ]; then
            pkexec bash -c '
                systemctl stop gnumond.service 2>/dev/null || true
                systemctl disable gnumond.service 2>/dev/null || true
                rm -f /etc/systemd/system/gnumond.service
                systemctl daemon-reload
            ' 2>/dev/null || true
        fi

        # 3. Remove udev input permissions if present
        if [ -f "/etc/udev/rules.d/99-gnumon-input.rules" ]; then
            pkexec bash -c '
                rm -f /etc/udev/rules.d/99-gnumon-input.rules
                udevadm control --reload-rules
                udevadm trigger -s input -c change
            ' 2>/dev/null || true
        fi

        # 4. Run uninstall-layers.sh
        for s in "$SCRIPT_DIR/uninstall-layers.sh" "$ROOT_DIR/scripts/uninstall-layers.sh"; do
            if [ -f "$s" ]; then
                bash "$s"
                break
            fi
        done

        echo "==> gnumon services, layers, and permissions have been completely uninstalled!"
        ;;

    *)
        echo "Usage: $0 {status|enable-user|disable-user|start-user|stop-user|install-udev|install-system|install-layers|uninstall-all}"
        exit 1
        ;;
esac
