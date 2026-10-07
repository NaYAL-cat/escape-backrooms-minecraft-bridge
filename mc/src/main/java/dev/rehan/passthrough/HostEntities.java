package dev.rehan.passthrough;
import com.google.gson.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicReference;
import net.minecraft.server.MinecraftServer;
import net.minecraft.world.entity.*;
import net.minecraft.world.entity.npc.villager.Villager;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.effect.*;
import net.minecraft.world.damagesource.DamageSource;

/** Vanilla hit detection/cooldown/projectiles against host pawns; damage is forwarded after vanilla accepts it. */
public final class HostEntities {
    public static final String TAG="etb_v2_proxy";
    private static final Map<Integer,Villager> proxies=new HashMap<>();
    private static final Map<Integer,Integer> ids=new HashMap<>();
    private static final AtomicReference<JsonArray> pending=new AtomicReference<>();
    private static long updated;
    public static boolean known(Entity e) { return ids.containsKey(e.getId()); }
    public static boolean isProxy(Entity e) { return e.entityTags().contains(TAG) || (e instanceof Villager && e.isInvisible() && e.getCustomName()!=null && e.getCustomName().getString().equals("ETB proxy")); }
    public static void receive(JsonArray a) { if(a.size()<=128) { pending.set(a); updated=System.nanoTime(); } }
    public static void tick(MinecraftServer s) {
        if(System.nanoTime()-updated>1_000_000_000L) { clear(); return; }
        JsonArray a=pending.getAndSet(null); if(a==null) return;
        Set<Integer> seen=new HashSet<>(); var level=s.overworld();
        for(var value:a) {
            JsonArray row=value.getAsJsonArray(); if(row.size()!=6) continue;
            int id=row.get(0).getAsInt(); seen.add(id);
            Villager e=proxies.get(id);
            if(e==null || e.isRemoved()) {
                e=EntityTypes.VILLAGER.create(level,EntitySpawnReason.COMMAND); if(e==null) continue;
                e.addTag(TAG); e.setCustomName(net.minecraft.network.chat.Component.literal("ETB proxy")); e.setCustomNameVisible(false);
                e.setNoAi(true); e.setNoGravity(true); e.setSilent(true); e.setInvisible(true); e.noPhysics=true;
                e.addEffect(new MobEffectInstance(MobEffects.INVISIBILITY,MobEffectInstance.INFINITE_DURATION,0,false,false),null);
                e.getAttribute(Attributes.MAX_HEALTH).setBaseValue(1024); e.setHealth(e.getMaxHealth());
                e.getAttribute(Attributes.KNOCKBACK_RESISTANCE).setBaseValue(1);
                e.setPos(row.get(1).getAsDouble(),row.get(2).getAsDouble(),row.get(3).getAsDouble());
                proxies.put(id,e); ids.put(e.getId(),id); level.addFreshEntity(e);
            }
            e.setPos(row.get(1).getAsDouble(),row.get(2).getAsDouble(),row.get(3).getAsDouble());
            e.setDeltaMovement(net.minecraft.world.phys.Vec3.ZERO);
            // Scale proxy height to the measured capsule/bounds; MC scale is uniform, so broad shapes remain approximate.
            var scale=e.getAttribute(Attributes.SCALE); if(scale!=null) scale.setBaseValue(Math.clamp(row.get(5).getAsDouble()/1.95,.2,4));
            e.setHealth(e.getMaxHealth());
        }
        var it=proxies.entrySet().iterator();
        while(it.hasNext()) { var e=it.next(); if(!seen.contains(e.getKey())) { ids.remove(e.getValue().getId()); e.getValue().discard(); it.remove(); } }
    }
    public static void hit(LivingEntity e,DamageSource source,float amount) {
        Integer id=ids.get(e.getId());
        if(id==null || !Float.isFinite(amount) || amount<=0 || source.getEntity()==null) return;
        JsonObject m=new JsonObject(); m.addProperty("t","damage"); m.addProperty("id",id); m.addProperty("amount",amount); Passthrough.events.accept(m.toString());
    }
    public static void clear() { for(var e:proxies.values()) e.discard(); proxies.clear(); ids.clear(); pending.set(null); }
}
