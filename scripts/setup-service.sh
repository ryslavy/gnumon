#!/usr/bin/env bash
# setup-service.sh — Service and permissions manager for gnumon (Linux PresentMon)
# Manages systemd user/system service, udev input rules, and Vulkan layer registration.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ACTION="${1:-status}"

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
        if [ -r "/dev/input/event0" ] || [ -r "/dev/input/event1" ] || [ -r "/dev/input/event2" ]; then
            INPUT_ACCESS="yes"
        fi

        # 5. Vulkan layers status (64-bit and 32-bit)
        VK64_INSTALLED="no"
        VK32_INSTALLED="no"
        if [ -f "$HOME/.local/share/vulkan/implicit_layer.d/VkLayer_gnumon.json" ] || \
           [ -f "$HOME/.local/share/vulkan/implicit_layer.d/VkLayer_gnumon.x86_64.json" ]; then
            VK64_INSTALLED="yes"
        fi
        if [ -f "$HOME/.local/share/vulkan/implicit_layer.d/VkLayer_gnumon.i686.json" ] || \
           [ -f "$HOME/.local/share/vulkan/implicit_layer.d/VkLayer_gnumon.x86.json" ]; then
            VK32_INSTALLED="yes"
        fi

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
        echo "==> User service stopped."
        ;;

    start-user)
        systemctl --user start gnumond.service
        echo "==> User service started."
        ;;

    stop-user)
        systemctl --user stop gnumond.service 2>/dev/null || true
        echo "==> User service stopped."
        ;;

    install-udev)
        echo "==> Installing udev rules using pkexec..."
        RULES_SRC=""
        for r in "$SCRIPT_DIR/99-gnumon-input.rules" \
                 "$SCRIPT_DIR/../lib/udev/rules.d/99-gnumon-input.rules" \
                 "$SCRIPT_DIR/../lib/udev/99-gnumon-input.rules" \
                 "$ROOT_DIR/scripts/99-gnumon-input.rules" \
                 "/usr/lib/udev/rules.d/99-gnumon-input.rules"; do
            if [ -f "$r" ]; then
                RULES_SRC="$r"
                break
            fi
        done

        if [ -z "$RULES_SRC" ]; then
            echo "Error: 99-gnumon-input.rules not found" >&2
            exit 1
        fi
        pkexec bash -c "cp -f '$RULES_SRC' /etc/udev/rules.d/99-gnumon-input.rules && udevadm control --reload-rules && udevadm trigger"
        echo "==> Udev rules installed and reloaded successfully!"
        ;;

    install-system)
        echo "==> Installing system-wide daemon and udev rules using pkexec..."
        RULES_SRC=""
        for r in "$SCRIPT_DIR/99-gnumon-input.rules" \
                 "$SCRIPT_DIR/../lib/udev/rules.d/99-gnumon-input.rules" \
                 "$SCRIPT_DIR/../lib/udev/99-gnumon-input.rules" \
                 "$ROOT_DIR/scripts/99-gnumon-input.rules" \
                 "/usr/lib/udev/rules.d/99-gnumon-input.rules"; do
            if [ -f "$r" ]; then
                RULES_SRC="$r"
                break
            fi
        done

        SERVICE_SRC=""
        for s in "$ROOT_DIR/systemd/gnumond.service" \
                 "$SCRIPT_DIR/../share/systemd/user/gnumond.service" \
                 "$SCRIPT_DIR/../systemd/gnumond.service" \
                 "/usr/share/systemd/user/gnumond.service"; do
            if [ -f "$s" ]; then
                SERVICE_SRC="$s"
                break
            fi
        done

        pkexec bash -c "
            if [ -n '$RULES_SRC' ]; then
                cp -f '$RULES_SRC' /etc/udev/rules.d/99-gnumon-input.rules
                udevadm control --reload-rules && udevadm trigger
            fi &&
            if [ -n '$SERVICE_SRC' ]; then
                cp -f '$SERVICE_SRC' /etc/systemd/system/gnumond.service &&
                systemctl daemon-reload &&
                systemctl enable --now gnumond.service
            fi
        "
        echo "==> System service and udev rules installed and activated!"
        ;;

    install-layers)
        echo "==> Running Vulkan layers installer..."
        "$SCRIPT_DIR/install-layers.sh"
        ;;

    *)
        echo "Usage: $0 {status|enable-user|disable-user|start-user|stop-user|install-udev|install-system|install-layers}"
        exit 1
        ;;
esac
