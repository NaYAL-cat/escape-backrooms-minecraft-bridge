package dev.rehan.passthrough;
import com.google.gson.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicReference;
import java.nio.file.*;
import net.minecraft.core.BlockPos;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.*;
import net.minecraft.world.item.*;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.block.*;
import net.minecraft.world.level.storage.LevelResource;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.shapes.*;

/** Only the private integrated V2 world is modified. Native vanilla gameplay remains in charge. */
public final class BridgeWorld {
    public static final String WORLD="escape-backrooms-bridge-v2";
    private static volatile MinecraftServer server;
    private static volatile Map<BlockPos,VoxelShape> shapes=Map.of();
    private static Set<BlockPos> placed=new HashSet<>();
    private static final AtomicReference<JsonArray> pendingCollision=new AtomicReference<>();
    private static final Map<UUID,SafeSpot> safeSpots=new HashMap<>();
    private static Map<BlockPos,VoxelShape> protectedSupport=Map.of();
    private static long supportUntil,lastGeometry,lastReconcile;
    private static boolean geometryUsable;
    private static JsonArray lastAppliedCollision;
    private record SafeSpot(Vec3 feet, Map<BlockPos,VoxelShape> support) {}

    private BridgeWorld() {}
    public static boolean ready() { return server!=null; }
    public static MinecraftServer server() { return server; }
    public static VoxelShape shape(BlockPos pos) { return shapes.getOrDefault(pos,Shapes.empty()); }
    public static void attach(MinecraftServer s) {
        if(s.isSingleplayer() && s.getWorldPath(LevelResource.ROOT).normalize().getFileName().toString().equals(WORLD)) server=s;
    }
    public static void detach() {
        HostEntities.clear(); server=null; shapes=Map.of(); placed.clear();
        pendingCollision.set(null);safeSpots.clear();protectedSupport=Map.of();
        lastGeometry=supportUntil=lastReconcile=0;geometryUsable=false;lastAppliedCollision=null;Passthrough.minecraftDrive=false;
    }
    public static void tick(MinecraftServer s) {
        if(s!=server) return;
        applyPendingCollision();
        long now=System.nanoTime();
        boolean fresh=geometryUsable && now-lastGeometry<750_000_000L;
        for(ServerPlayer p:s.getPlayerList().getPlayers()) {
            boolean hostWorld=p.level().dimension().equals(Level.OVERWORLD);
            boolean waiting=!Passthrough.minecraftDrive || (hostWorld && !fresh);
            if(hostWorld && p.isAlive() && !p.isSpectator()) {
                boolean supported=hasSupport(p,.15);
                if(supported && !p.getAbilities().flying && p.getDeltaMovement().y<=.08 &&
                        p.level().noCollision(p,p.getBoundingBox().deflate(1e-5)))
                    safeSpots.put(p.getUUID(),new SafeSpot(p.position(),HostGeometry.supportSnapshot(shapes,p.position())));
                SafeSpot safe=safeSpots.get(p.getUUID());
                if(safe!=null && HostGeometry.needsRecovery(p.getY(),safe.feet.y,supported,hasSupport(p,3.0),
                        p.getAbilities().flying,fresh)) {
                    // Restore only floor geometry actually observed at the last valid landing, never a fabricated plane.
                    protectedSupport=safe.support;supportUntil=now+3_000_000_000L;
                    Map<BlockPos,VoxelShape> restored=new HashMap<>(shapes);
                    safe.support.forEach((pos,shape)->restored.merge(pos,shape,Shapes::or));
                    publish(restored);
                    p.teleportTo(s.overworld(),safe.feet.x,safe.feet.y+.02,safe.feet.z,Set.of(),p.getYRot(),p.getXRot(),true);
                    p.setDeltaMovement(Vec3.ZERO);p.fallDistance=0;
                    Passthrough.LOG.warn("Recovered {} to last supported position {} after missing host ground",p.getScoreboardName(),safe.feet);
                    Passthrough.events.accept("{\"t\":\"recovered\",\"reason\":\"missing_host_ground\"}");
                }
            }
            p.setNoGravity(waiting);
            if(waiting) { p.setDeltaMovement(Vec3.ZERO); p.fallDistance=0; }
        }
        HostEntities.tick(s);
    }
    public static void mode(GameType mode) { var s=server; if(s!=null) s.execute(()->s.getPlayerList().getPlayers().forEach(p->p.setGameMode(mode))); }
    public static void align(JsonObject m,Runnable done) {
        var s=server; if(s==null) return;
        JsonArray a=m.getAsJsonArray("p"); double x=a.get(0).getAsDouble(),y=a.get(1).getAsDouble(),z=a.get(2).getAsDouble();
        float yaw=m.get("yaw").getAsFloat(),pitch=m.get("pitch").getAsFloat();
        if(!Double.isFinite(x+y+z) || y< -60 || y>315 || Math.abs(x)>28_000_000 || Math.abs(z)>28_000_000) return;
        s.execute(()-> {
            applyPendingCollision();
            if(!geometryUsable) return;
            safeSpots.clear();protectedSupport=Map.of();supportUntil=0;
            boolean grounded=true;
            for(ServerPlayer p:s.getPlayerList().getPlayers()) {
                p.teleportTo(s.overworld(),x,y+.02,z,Set.of(),yaw,pitch,true);
                p.setDeltaMovement(Vec3.ZERO); p.fallDistance=0;
                if(!hasSupport(p,.3)) { grounded=false; continue; }
                safeSpots.put(p.getUUID(),new SafeSpot(p.position(),HostGeometry.supportSnapshot(shapes,p.position())));
                starter(s,p);
                // A safe home for vanilla death/respawn, with the current host level's anchor.
                s.getCommands().performPrefixedCommand(s.createCommandSourceStack(),"spawnpoint "+p.getScoreboardName()+" "+(int)Math.floor(x)+" "+(int)Math.ceil(y)+" "+(int)Math.floor(z));
            }
            if(grounded) done.run();
        });
    }
    private static void starter(MinecraftServer s,ServerPlayer p) {
        Path marker=s.getWorldPath(LevelResource.ROOT).resolve("bridge-starter-"+p.getUUID()+".done");
        if(Files.exists(marker)) return;
        for(ItemStack item:List.of(new ItemStack(Items.OAK_LOG,32),new ItemStack(Items.COBBLESTONE,64),new ItemStack(Items.IRON_INGOT,32),
            new ItemStack(Items.COAL,32),new ItemStack(Items.BREAD,32),new ItemStack(Items.TORCH,32),new ItemStack(Items.OAK_SAPLING,8),
            new ItemStack(Items.DIRT,32),new ItemStack(Items.WATER_BUCKET),new ItemStack(Items.LAVA_BUCKET),new ItemStack(Items.WHEAT_SEEDS,8))) p.getInventory().add(item);
        try { Files.writeString(marker,"V2 starter supplied once; inventory is never cleared.\n"); }
        catch(Exception e) { Passthrough.LOG.error("Cannot persist starter marker",e); }
    }
    public static void collision(JsonArray boxes) {
        if(server!=null && boxes.size()<=4096) pendingCollision.set(boxes);
    }
    private static boolean hasSupport(ServerPlayer player,double drop) {
        AABB body=player.getBoundingBox();
        AABB probe=new AABB(body.minX+1e-5,body.minY-drop,body.minZ+1e-5,body.maxX-1e-5,body.minY+.01,body.maxZ-1e-5);
        for(VoxelShape shape:player.level().getBlockCollisions(player,probe))
            if(HostGeometry.footing(shape,probe,body.minY)) return true;
        return false;
    }
    private static void applyPendingCollision() {
        JsonArray boxes=pendingCollision.getAndSet(null);
        if(boxes==null) return;
        if(boxes.equals(lastAppliedCollision) && protectedSupport.isEmpty()) {
            lastGeometry=System.nanoTime();geometryUsable=true;
            // Player building/mining can change a cell even while the host snapshot is unchanged.
            if(lastGeometry-lastReconcile>250_000_000L) publish(shapes);
            return;
        }
        Map<BlockPos,VoxelShape> next;
        try { next=HostGeometry.parse(boxes); }
        catch(RuntimeException error) {
            geometryUsable=false;lastGeometry=System.nanoTime();
            Passthrough.LOG.warn("Ignoring invalid host collision snapshot",error);return;
        }
        lastGeometry=System.nanoTime();geometryUsable=next!=null && !next.isEmpty();
        // Empty/invalid data means unavailable, not permission to erase the last known floor.
        if(!geometryUsable) return;
        lastAppliedCollision=boxes;
        if(lastGeometry<supportUntil)
            protectedSupport.forEach((pos,shape)->next.merge(pos,shape,Shapes::or));
        else protectedSupport=Map.of();
        publish(next);
    }
    private static void publish(Map<BlockPos,VoxelShape> next) {
        ServerLevel level=server.overworld();
        lastReconcile=System.nanoTime();
        // Known old geometry remains visible until the replacement is ready; conversion runs on the server thread.
        shapes=Map.copyOf(next);
        for(BlockPos old:placed) if(!next.containsKey(old) && level.getBlockState(old).is(HostCollisionBlock.INSTANCE))
            level.setBlock(old,Blocks.AIR.defaultBlockState(),Block.UPDATE_CLIENTS|Block.UPDATE_KNOWN_SHAPE);
        Set<BlockPos> now=new HashSet<>();
        for(BlockPos pos:next.keySet()) {
            var state=level.getBlockState(pos);
            if(state.isAir() || state.is(HostCollisionBlock.INSTANCE)) {
                if(state.isAir()) level.setBlock(pos,HostCollisionBlock.INSTANCE.defaultBlockState(),Block.UPDATE_CLIENTS|Block.UPDATE_KNOWN_SHAPE);
                now.add(pos);
            }
        }
        placed=now;
    }
}
