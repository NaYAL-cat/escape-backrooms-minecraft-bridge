package dev.rehan.passthrough.client;

import dev.rehan.passthrough.Passthrough;

import java.util.List;
import java.util.Optional;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.networking.v1.ServerPlayConnectionEvents;
import net.minecraft.client.CloudStatus;
import net.minecraft.client.InactivityFpsLimit;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Options;
import net.minecraft.client.gui.screens.DeathScreen;
import net.minecraft.client.gui.screens.TitleScreen;
import net.minecraft.client.tutorial.TutorialSteps;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.HolderSet;
import net.minecraft.core.registries.Registries;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.LevelSettings;
import net.minecraft.world.level.WorldDataConfiguration;
import net.minecraft.world.level.biome.Biomes;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.levelgen.FlatLevelSource;
import net.minecraft.world.level.levelgen.WorldDimensions;
import net.minecraft.world.level.levelgen.WorldOptions;
import net.minecraft.world.level.levelgen.flat.FlatLayerInfo;
import net.minecraft.world.level.levelgen.flat.FlatLevelGeneratorSettings;
import net.minecraft.world.level.levelgen.presets.WorldPresets;

public class PassthroughClient implements ClientModInitializer {
    private static final String WORLD=dev.rehan.passthrough.BridgeWorld.WORLD;
    private static boolean configured,worldRequested;
    private static int statusTicks;
    @Override public void onInitializeClient() {
        HostLink.launch();
        ClientTickEvents.END_CLIENT_TICK.register(PassthroughClient::tick);
    }
    private static void tick(Minecraft mc) {
        ClientInput.tick(mc);
        HostState.beginFrame();
        if(++statusTicks%10==0) HostLink.send("{\"t\":\"status\",\"ready\":"+(dev.rehan.passthrough.BridgeWorld.ready() && mc.player!=null)+"}");
        if(!configured) { configured=true; configure(mc.options); }
        if(!worldRequested && mc.level==null && mc.gui.screen() instanceof TitleScreen && !Boolean.getBoolean("passthrough.noAutoWorld")) {
            worldRequested=true; openWorld(mc);
        }
    }

	/** Settings for sitting behind another game: keep running unfocused, and no sky/cloud/bobbing effects in the picture. */
	private static void configure(final Options options) {
		options.pauseOnLostFocus = false;
		options.onboardAccessibility = false;
		options.tutorialStep = TutorialSteps.NONE;
		options.cloudStatus().set(CloudStatus.OFF);
		options.bobView().set(false);
		options.vignette().set(false);
		options.improvedTransparency().set(false);
		options.inactivityFpsLimit().set(InactivityFpsLimit.MINIMIZED);
		options.fovEffectScale().set(0.0);
		options.damageTiltStrength().set(0.0);
		options.menuBackgroundBlurriness().set(0);
		options.enableVsync().set(false);
		// 26.3 treats this constant as unlimited, not a 260 FPS cap.
		options.framerateLimit().set(Options.UNLIMITED_FRAMERATE_CUTOFF);
		// Do not persist bridge rendering choices into the user profile.
	}

	private static void openWorld(final Minecraft minecraft) {
		if (minecraft.getLevelSource().levelExists(WORLD)) {
			Passthrough.LOG.info("opening world {}", WORLD);
			minecraft.createWorldOpenFlows().openWorld(WORLD, () -> minecraft.gui.setScreen(new TitleScreen()));
		} else {
			Passthrough.LOG.info("creating world {}", WORLD);
			LevelSettings settings = new LevelSettings("Escape the Backrooms × Minecraft V2", GameType.SURVIVAL, LevelSettings.DifficultySettings.DEFAULT, true, WorldDataConfiguration.DEFAULT);
			minecraft.createWorldOpenFlows().createFreshLevel(WORLD, settings, new WorldOptions(0L, true, false), PassthroughClient::voidWorld, minecraft.gui.screen());
		}
	}

	/** A flat world with a single layer of air: nothing but what gets built (the host's ground arrives as barriers). */
	private static WorldDimensions voidWorld(final HolderLookup.Provider registries) {
		FlatLevelGeneratorSettings flat = new FlatLevelGeneratorSettings(
			Optional.of(HolderSet.direct()), registries.lookupOrThrow(Registries.BIOME).getOrThrow(Biomes.PLAINS), List.of()
		);
		flat.getLayersInfo().add(new FlatLayerInfo(1, Blocks.AIR));
		flat.updateLayers();
		return WorldPresets.createNormalWorldDimensions(registries).replaceOverworldGenerator(registries, new FlatLevelSource(flat));
	}
}
