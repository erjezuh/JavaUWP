package banditvault.audio;

import javax.sound.sampled.Mixer;
import javax.sound.sampled.spi.MixerProvider;

/**
 * javax.sound SPI entry point. Discovered through
 * META-INF/services/javax.sound.sampled.spi.MixerProvider on the game
 * classpath; exposes the UWP microphone to any mod that opens a
 * TargetDataLine (voice chat mods).
 */
public final class BanditMixerProvider extends MixerProvider {

    @Override
    public Mixer.Info[] getMixerInfo() {
        return new Mixer.Info[]{ BanditMixer.mixerInfo() };
    }

    @Override
    public Mixer getMixer(Mixer.Info info) {
        if (info == null) {
            throw new NullPointerException("info");
        }
        return new BanditMixer();
    }

    @Override
    public boolean isMixerSupported(Mixer.Info info) {
        if (info == null) return false;
        final Mixer.Info mine = BanditMixer.mixerInfo();
        return mine.getName() != null && mine.getName().equals(info.getName())
                && mine.getVendor() != null && mine.getVendor().equals(info.getVendor())
                && mine.getVersion() != null && mine.getVersion().equals(info.getVersion());
    }
}
