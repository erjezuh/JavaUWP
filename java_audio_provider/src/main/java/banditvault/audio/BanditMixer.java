package banditvault.audio;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.Control;
import javax.sound.sampled.Control.Type;
import javax.sound.sampled.DataLine;
import javax.sound.sampled.Line;
import javax.sound.sampled.LineEvent;
import javax.sound.sampled.LineListener;
import javax.sound.sampled.LineUnavailableException;
import javax.sound.sampled.Mixer;
import java.util.ArrayList;
import java.util.List;

/**
 * One-target-line mixer exposing the UWP microphone to javax.sound.
 * Implements exactly the Java 8 Mixer/Line contracts so the same jar loads on
 * the Java 8 runtime (1.12.2) and the modern runtime.
 */
public final class BanditMixer implements Mixer {

    static final Mixer.Info MIXER_INFO = new Mixer.Info(
            "Bandit Microphone", "Bandit Launcher", "UWP microphone capture for Bandit Launcher", "1.0") {};

    static Mixer.Info mixerInfo() {
        return MIXER_INFO;
    }

    private final Object lock = new Object();
    private final List<LineListener> listeners = new ArrayList<LineListener>();
    private boolean open;
    private BanditMicTargetDataLine line;

    BanditMixer() {}

    private BanditMicTargetDataLine theLine() {
        synchronized (lock) {
            if (line == null) line = new BanditMicTargetDataLine();
            return line;
        }
    }

    // ---- Mixer ----------------------------------------------------------

    @Override
    public Mixer.Info getMixerInfo() {
        return MIXER_INFO;
    }

    @Override
    public Line.Info[] getSourceLineInfo() {
        return new Line.Info[0];
    }

    @Override
    public Line.Info[] getSourceLineInfo(Line.Info info) {
        return new Line.Info[0];
    }

    @Override
    public Line.Info[] getTargetLineInfo() {
        return new Line.Info[]{ BanditMicTargetDataLine.info() };
    }

    @Override
    public Line.Info[] getTargetLineInfo(Line.Info info) {
        return isLineSupported(info) ? getTargetLineInfo() : new Line.Info[0];
    }

    @Override
    public boolean isLineSupported(Line.Info info) {
        if (info == null) return false;
        final Class<?> cls = info.getLineClass();
        if (cls != TargetDataLineClass.TARGET && cls != TargetDataLineClass.LINE
                && cls != TargetDataLineClass.DATA
                && !TargetDataLineClass.TARGET.isAssignableFrom(cls)) {
            return false;
        }
        if (info instanceof DataLine.Info) {
            final AudioFormat[] formats = ((DataLine.Info) info).getFormats();
            if (formats != null && formats.length > 0) {
                boolean any = false;
                for (AudioFormat format : formats) {
                    if (format == null || BanditMicTargetDataLine.formatSupported(format)) {
                        any = true;
                        break;
                    }
                }
                if (!any) return false;
            }
        }
        return true;
    }

    @Override
    public Line getLine(Line.Info info) throws LineUnavailableException {
        if (!isLineSupported(info)) {
            throw new LineUnavailableException("Unsupported line: " + info);
        }
        return theLine();
    }

    @Override
    public int getMaxLines(Line.Info info) {
        return isLineSupported(info) ? 1 : 0;
    }

    @Override
    public Line[] getSourceLines() {
        return new Line[0];
    }

    @Override
    public Line[] getTargetLines() {
        synchronized (lock) {
            return line != null ? new Line[]{ line } : new Line[0];
        }
    }

    @Override
    public void synchronize(Line[] lines, boolean maintainSync) {
        throw new IllegalArgumentException("Synchronization is not supported");
    }

    @Override
    public void unsynchronize(Line[] lines) {
        throw new IllegalArgumentException("Synchronization is not supported");
    }

    @Override
    public boolean isSynchronizationSupported(Line[] lines, boolean maintainSync) {
        return false;
    }

    // ---- Line (a Mixer is a Line) ----------------------------------------

    @Override
    public Line.Info getLineInfo() {
        return new Line.Info(Mixer.class);
    }

    @Override
    public void open() throws LineUnavailableException {
        synchronized (lock) {
            if (open) return;
            theLine();
            open = true;
        }
        fire(new LineEvent(this, LineEvent.Type.OPEN, AudioSystem.NOT_SPECIFIED));
    }

    @Override
    public void close() {
        final BanditMicTargetDataLine l;
        synchronized (lock) {
            if (!open) return;
            open = false;
            l = line;
        }
        if (l != null) l.close();
        fire(new LineEvent(this, LineEvent.Type.CLOSE, AudioSystem.NOT_SPECIFIED));
    }

    @Override
    public boolean isOpen() {
        synchronized (lock) {
            return open;
        }
    }

    @Override
    public Control[] getControls() {
        return new Control[0];
    }

    @Override
    public boolean isControlSupported(Type type) {
        return false;
    }

    @Override
    public Control getControl(Type type) {
        throw new IllegalArgumentException("Unsupported control: " + type);
    }

    @Override
    public void addLineListener(LineListener listener) {
        if (listener == null) return;
        synchronized (listeners) {
            if (!listeners.contains(listener)) listeners.add(listener);
        }
    }

    @Override
    public void removeLineListener(LineListener listener) {
        synchronized (listeners) {
            listeners.remove(listener);
        }
    }

    private void fire(LineEvent event) {
        final List<LineListener> copy;
        synchronized (listeners) {
            copy = new ArrayList<LineListener>(listeners);
        }
        for (LineListener listener : copy) {
            listener.update(event);
        }
    }

    /** Line-class constants, kept in a tiny holder to avoid repeated class literals. */
    private static final class TargetDataLineClass {
        static final Class<?> TARGET = javax.sound.sampled.TargetDataLine.class;
        static final Class<?> DATA = DataLine.class;
        static final Class<?> LINE = Line.class;
    }
}
