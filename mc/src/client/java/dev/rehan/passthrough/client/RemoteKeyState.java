package dev.rehan.passthrough.client;

import dev.rehan.passthrough.client.mixin.KeyMappingAccessor;
import java.util.HashSet;
import java.util.Set;
import net.minecraft.client.KeyMapping;

/** Physical edges, vanilla hold/toggle behavior, and an unconditional bridge disconnect release. */
final class RemoteKeyState {
    private final Set<KeyMapping> held = new HashSet<>();
    // Toggled actions may stay down after the physical button has been released.
    private final Set<KeyMapping> touched = new HashSet<>();

    void set(KeyMapping key, boolean down, boolean repeat) {
        if (down) {
            // A repeat after focus/screen release is not a new press.
            if (repeat || !held.add(key)) return;
            touched.add(key);
            KeyMappingAccessor access = (KeyMappingAccessor) key;
            access.passthrough$setClickCount(access.passthrough$getClickCount() + 1);
            key.setDown(true);
        } else if (held.remove(key)) {
            key.setDown(false);
        }
    }

    void release() {
        for (KeyMapping key : touched) {
            KeyMappingAccessor access = (KeyMappingAccessor) key;
            // setDown(false) intentionally does nothing for vanilla toggle bindings.
            access.passthrough$setDown(false);
            access.passthrough$setClickCount(0);
        }
        held.clear();
        touched.clear();
    }
}
