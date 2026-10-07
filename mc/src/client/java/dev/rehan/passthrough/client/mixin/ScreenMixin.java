package dev.rehan.passthrough.client.mixin;

import dev.rehan.passthrough.client.ClientInput;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.gui.screens.Screen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Draws a software cursor into the exported Minecraft UI while Escape the Backrooms owns the real mouse. */
@Mixin(Screen.class)
abstract class ScreenMixin {
	@Inject(method = "extractRenderStateWithTooltipAndSubtitles", at = @At("TAIL"))
	private void passthrough$remoteCursor(final GuiGraphicsExtractor graphics, final int mouseX, final int mouseY,
		final float partialTick, final CallbackInfo ci) {
		if (!ClientInput.remoteCursorVisible()) {
			return;
		}

		Minecraft minecraft = Minecraft.getInstance();
		int x = ClientInput.remoteCursorX(minecraft.getWindow().getGuiScaledWidth());
		int y = ClientInput.remoteCursorY(minecraft.getWindow().getGuiScaledHeight());
		// A small outlined arrow remains visible over both dark and bright inventory slots.
		graphics.fill(x, y, x + 2, y + 13, 0xFF000000);
		graphics.fill(x, y, x + 9, y + 2, 0xFF000000);
		graphics.fill(x + 1, y + 1, x + 7, y + 2, 0xFFFFFFFF);
		graphics.fill(x + 1, y + 2, x + 2, y + 11, 0xFFFFFFFF);
		graphics.fill(x + 2, y + 7, x + 6, y + 8, 0xFFFFFFFF);
	}
}
