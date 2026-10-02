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
import javax.sound.sampled.TargetDataLine;
import java.util.ArrayList;
import java.util.List;

/**
 * TargetDataLine backed by the UWP microphone capture in the glfw shim.
 * Implements exactly the Java 8 TargetDataLine/DataLine/Line contracts so the
 * same jar loads on the Java 8 runtime (1.12.2) and the modern runtime.
 */
public final class BanditMicTargetDataLine implements TargetDataLine {

    static final AudioFormat[] SUPPORTED = {
        new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, 48000f, 16, 1, 2, 48000f, false),
        new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, 48000f, 16, 2, 4, 48000f, false),
        new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, 44100f, 16, 1, 2, 44100f, false),
        new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, 44100f, 16, 2, 4, 44100f, false),
        new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, 16000f, 16, 1, 2, 16000f, false),
        new AudioFormat(AudioFormat.Encoding.PCM_SIGNED, 8000f, 16, 1, 2, 8000f, false),
    };

    static DataLine.Info info() {
        return new DataLine.Info(TargetDataLine.class, SUPPORTED, AudioSystem.NOT_SPECIFIED, AudioSystem.NOT_SPECIFIED);
    }

    static boolean formatSupported(AudioFormat format) {
        if (format == null) return false;
        if (!AudioFormat.Encoding.PCM_SIGNED.equals(format.getEncoding())) return false;
        if (format.getSampleSizeInBits() != 16) return false;
        if (format.isBigEndian()) return false;
        final int ch = format.getChannels();
        if (ch < 1 || ch > 2) return false;
        final float rate = format.getSampleRate();
        return rate >= 8000f && rate <= 48000f;
    }

    private final Object lock = new Object();
    private final List<LineListener> listeners = new ArrayList<LineListener>();

    private boolean open;
    private boolean running;
    private AudioFormat openFormat = SUPPORTED[0];
    private int bufferSize = 16384;
    private long bytesRead;

    BanditMicTargetDataLine() {}

    // ---- TargetDataLine -------------------------------------------------

    @Override
    public int read(byte[] buf, int off, int len) {
        synchronized (lock) {
            if (!open) throw new IllegalStateException("Line is not open");
        }
        if (buf == null) throw new NullPointerException("buf");
        if (off < 0 || len < 0 || off + len > buf.length) throw new IndexOutOfBoundsException();
        if (len == 0) return 0;
        final int got = BanditMic.nativeRead(buf, off, len, 250);
        if (got > 0) {
            synchronized (lock) {
                bytesRead += got;
            }
        }
        return got < 0 ? 0 : got;
    }

    // ---- TargetDataLine.open overloads ----------------------------------

    @Override
    public void open(AudioFormat format) throws LineUnavailableException {
        open(format, getBufferSize());
    }

    @Override
    public void open(AudioFormat format, int size) throws LineUnavailableException {
        if (!formatSupported(format)) {
            throw new LineUnavailableException("Unsupported format: " + format);
        }
        synchronized (lock) {
            if (open) {
                throw new IllegalStateException("Line is already open");
            }
            if (!BanditMic.isAvailable()) {
                throw new LineUnavailableException("Microphone native bridge not loaded: " + BanditMic.loadError());
            }
            final int rate = Math.round(format.getSampleRate());
            final int channels = format.getChannels();
            final int rc = BanditMic.nativeOpen(rate, channels, 16);
            if (rc != 1) {
                throw new LineUnavailableException("UWP microphone capture could not be opened (banditMicOpen=" + rc + ")");
            }
            openFormat = format;
            bufferSize = size > 0 ? size : Math.max(4096, rate * channels * 2 / 10);
            bytesRead = 0;
            open = true;
        }
        fire(new LineEvent(this, LineEvent.Type.OPEN, AudioSystem.NOT_SPECIFIED));
    }

    // ---- Line ------------------------------------------------------------

    @Override
    public void open() throws LineUnavailableException {
        open(getFormat(), getBufferSize());
    }

    @Override
    public void close() {
        synchronized (lock) {
            if (!open) return;
            open = false;
            running = false;
        }
        try {
            BanditMic.nativeClose();
        } catch (Throwable ignored) {}
        fire(new LineEvent(this, LineEvent.Type.CLOSE, AudioSystem.NOT_SPECIFIED));
    }

    @Override
    public boolean isOpen() {
        synchronized (lock) {
            return open;
        }
    }

    @Override
    public Line.Info getLineInfo() {
        return info();
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

    // ---- DataLine ----------------------------------------------------------

    @Override
    public void start() {
        synchronized (lock) {
            if (!open || running) return;
            running = true;
        }
        fire(new LineEvent(this, LineEvent.Type.START, AudioSystem.NOT_SPECIFIED));
    }

    @Override
    public void stop() {
        synchronized (lock) {
            if (!running) return;
            running = false;
        }
        fire(new LineEvent(this, LineEvent.Type.STOP, AudioSystem.NOT_SPECIFIED));
    }

    @Override
    public boolean isRunning() {
        synchronized (lock) {
            return open && running;
        }
    }

    @Override
    public boolean isActive() {
        return isRunning();
    }

    @Override
    public void drain() {
        // Capture line: nothing to drain.
    }

    @Override
    public void flush() {
        final byte[] sink = new byte[8192];
        while (available() > 0) {
            if (BanditMic.nativeRead(sink, 0, sink.length, 0) <= 0) break;
        }
    }

    @Override
    public int available() {
        synchronized (lock) {
            if (!open) return 0;
        }
        return BanditMic.nativeAvailable();
    }

    @Override
    public int getBufferSize() {
        synchronized (lock) {
            return bufferSize;
        }
    }

    @Override
    public AudioFormat getFormat() {
        synchronized (lock) {
            return open ? openFormat : SUPPORTED[0];
        }
    }

    @Override
    public int getFramePosition() {
        final int frameSize = getFormat().getFrameSize();
        if (frameSize <= 0) return 0;
        synchronized (lock) {
            return (int) (bytesRead / frameSize);
        }
    }

    @Override
    public long getLongFramePosition() {
        final int frameSize = getFormat().getFrameSize();
        if (frameSize <= 0) return 0;
        synchronized (lock) {
            return bytesRead / frameSize;
        }
    }

    @Override
    public long getMicrosecondPosition() {
        final AudioFormat f = getFormat();
        final float frameRate = f.getFrameRate();
        if (frameRate <= 0f) return 0;
        synchronized (lock) {
            return (long) (bytesRead * 1000000.0 / frameRate);
        }
    }

    /** @deprecated retained for DataLine compatibility. */
    @Override
    @Deprecated
    public float getLevel() {
        return isRunning() ? 1.0f : 0.0f;
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
}
