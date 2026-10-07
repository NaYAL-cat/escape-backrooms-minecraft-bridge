package dev.rehan.passthrough;
import java.util.function.Consumer;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.*;
import org.slf4j.*;
public class Passthrough implements ModInitializer {
    public static final String ID="etb_minecraft_bridge";
    public static final Logger LOG=LoggerFactory.getLogger(ID);
    public static volatile boolean active,minecraftDrive;
    public static volatile Consumer<String> events=m->{};
    @Override public void onInitialize() {
        HostCollisionBlock.register();
        ServerLifecycleEvents.SERVER_STARTED.register(BridgeWorld::attach);
        ServerLifecycleEvents.SERVER_STOPPING.register(s->BridgeWorld.detach());
        ServerTickEvents.END_SERVER_TICK.register(BridgeWorld::tick);
        ServerEntityEvents.ENTITY_LOAD.register((e,l)-> { if(e.entityTags().contains(HostEntities.TAG) && !HostEntities.known(e)) e.discard(); });
        LOG.info("ETB Minecraft bridge V2 loaded");
    }
}
