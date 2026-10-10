package cpw.mods.cl;

import java.lang.reflect.Method;
import java.util.Collection;
import java.util.concurrent.CompletableFuture;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Label;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

/** Synthetic class-definition/bytecode-verifier tests: no Minecraft, Mixin or login. */
public final class UwpNativeGuardsTests implements Opcodes {
    private static final String PROBE = "net.caffeinemc.mods.sodium.client.compatibility.environment.probe.GraphicsAdapterProbe";
    private static final String SCANNER = "net.caffeinemc.mods.sodium.client.compatibility.checks.ModuleScanner";
    private static final String HAL = "oshi.hardware.platform.windows.WindowsHardwareAbstractionLayer";
    private static final String REPORT = "net.minecraft.SystemReport";
    private static final String HID = "dev.isxander.controlify.hid.ControllerHIDService";
    private static final String SDL = "dev.isxander.controlify.driver.sdl.SDLNativesLoader";
    private static final String OLD_SDL = "dev.isxander.controlify.driver.sdl.SDL3NativesManager";
    private static final String CONTROLIFY = "dev.isxander.controlify.Controlify";
    private static final String FUTURE = "()Ljava/util/concurrent/CompletableFuture;";

    public static void main(String[] args) throws Exception {
        byte[] probe = fixture(PROBE, true, "findAdapters", "()V", "getAdapters", "()Ljava/util/Collection;");
        System.clearProperty("banditvault.uwp");
        System.clearProperty("banditvault.neoforge.nativeGuards");
        same(probe, UwpNativeGuards.transform(PROBE, probe));
        System.setProperty("banditvault.uwp", "true");
        same(probe, UwpNativeGuards.transform(PROBE, probe));
        System.setProperty("banditvault.neoforge.nativeGuards", "true");
        System.clearProperty("banditvault.uwp");
        same(probe, UwpNativeGuards.transform(PROBE, probe));
        System.setProperty("banditvault.uwp", "true");
        byte[] empty = {};
        same(empty, UwpNativeGuards.transform(PROBE, empty));
        same(probe, UwpNativeGuards.transform("example.Unrelated", probe));
        byte[] wrongSignature = fixture(PROBE, true, "findAdapters", "(I)V");
        same(wrongSignature, UwpNativeGuards.transform(PROBE, wrongSignature));
        byte[] wrongAccess = fixture(PROBE, false, "findAdapters", "()V");
        same(wrongAccess, UwpNativeGuards.transform(PROBE, wrongAccess));
        try {
            UwpNativeGuards.transform(HAL, probe);
            throw new AssertionError("class identity mismatch accepted");
        } catch (IllegalArgumentException expected) {
            // A loader/class identity mismatch must not rewrite unrelated bytecode.
        }

        Class<?> p = load(PROBE, probe);
        p.getMethod("findAdapters").invoke(null);
        check(((Collection<?>) p.getMethod("getAdapters").invoke(null)).isEmpty(), "adapter inventory");
        // Transform twice: no duplicate members and still valid class files.
        load(PROBE, UwpNativeGuards.transform(PROBE, probe)).getMethod("findAdapters").invoke(null);
        Class<?> scanner = load(SCANNER, fixture(SCANNER, true, "listModules", "()Ljava/util/List;"));
        check(((Collection<?>) scanner.getMethod("listModules").invoke(null)).isEmpty(), "module inventory");
        Class<?> hal = load(HAL, fixture(HAL, false, "getGraphicsCards", "()Ljava/util/List;"));
        check(((Collection<?>) hal.getMethod("getGraphicsCards").invoke(hal.getConstructor().newInstance())).isEmpty(), "OSHI inventory");
        FixtureLoader loader = new FixtureLoader();
        Class<?> info = loader.define("oshi.SystemInfo", fixture("oshi.SystemInfo", false));
        Class<?> report = loader.define(REPORT, UwpNativeGuards.transform(REPORT, fixture(REPORT, false, "putHardware", "(Loshi/SystemInfo;)V")));
        report.getMethod("putHardware", info).invoke(report.getConstructor().newInstance(), info.getConstructor().newInstance());

        Class<?> controlify = load(CONTROLIFY, fixture(CONTROLIFY, false, "askNatives", FUTURE));
        Object cf = controlify.getMethod("askNatives").invoke(controlify.getConstructor().newInstance());
        check(Boolean.FALSE.equals(((CompletableFuture<?>) cf).join()), "Controlify GLFW selection");
        Class<?> old = load(OLD_SDL, fixture(OLD_SDL, true, "maybeLoad", FUTURE, "tryOfflineLoadAndStart", "()Z"));
        check(Boolean.FALSE.equals(((CompletableFuture<?>) old.getMethod("maybeLoad").invoke(null)).join()), "SDL download skipped");
        check(Boolean.FALSE.equals(old.getMethod("tryOfflineLoadAndStart").invoke(null)), "legacy SDL load skipped");
        Class<?> sdl = load(SDL, fixture(SDL, true, "tryLoad", "()Z"));
        check(Boolean.FALSE.equals(sdl.getMethod("tryLoad").invoke(null)), "SDL load skipped");
        check(sdl.getField("hasAttemptedLoad").getBoolean(null), "SDL onboarding completed");
        Class<?> hid = load(HID, fixture(HID, false, "start", "()V"));
        Object device = hid.getConstructor().newInstance();
        hid.getMethod("start").invoke(device);
        check(hid.getField("disabled").getBoolean(device), "HID queries disabled");
        check(!hid.getField("firstFetch").getBoolean(device), "intentional fallback must not show a first-fetch error");
        hid.getMethod("start").invoke(device);
        check(hid.getField("disabled").getBoolean(device), "repeated HID start");
        byte[] missingFields = fixtureWithFields(HID, false, false, "start", "()V");
        same(missingFields, UwpNativeGuards.transform(HID, missingFields));

        // Non-target rendering/capability code (with branch frames) survives exactly.
        for (Class<?> cls : new Class<?>[] {p, scanner, hal, report, controlify, old, sdl, hid}) {
            Method untouched = cls.getMethod("untouched", int.class);
            check(Integer.valueOf(42).equals(untouched.invoke(null, 1)), "untouched positive branch");
            check(Integer.valueOf(7).equals(untouched.invoke(null, 0)), "untouched zero branch");
        }
        System.out.println("NEOFORGE_NATIVE_GUARDS_TESTS_OK");
    }

    private static Class<?> load(String name, byte[] original) {
        return new FixtureLoader().define(name, UwpNativeGuards.transform(name, original));
    }

    private static byte[] fixture(String name, boolean isStatic, String... methods) {
        return fixtureWithFields(name, isStatic, true, methods);
    }

    private static byte[] fixtureWithFields(String name, boolean isStatic, boolean fields, String... methods) {
        String internal = name.replace('.', '/');
        ClassWriter out = new ClassWriter(ClassWriter.COMPUTE_FRAMES | ClassWriter.COMPUTE_MAXS);
        out.visit(V21, ACC_PUBLIC, internal, null, "java/lang/Object", null);
        if (fields && HID.equals(name)) {
            out.visitField(ACC_PUBLIC, "disabled", "Z", null, null).visitEnd();
            out.visitField(ACC_PUBLIC, "firstFetch", "Z", null, null).visitEnd();
        }
        if (fields && SDL.equals(name)) out.visitField(ACC_PUBLIC | ACC_STATIC, "hasAttemptedLoad", "Z", null, null).visitEnd();
        MethodVisitor constructor = out.visitMethod(ACC_PUBLIC, "<init>", "()V", null, null);
        constructor.visitCode();
        constructor.visitVarInsn(ALOAD, 0);
        constructor.visitMethodInsn(INVOKESPECIAL, "java/lang/Object", "<init>", "()V", false);
        if (fields && HID.equals(name)) {
            constructor.visitVarInsn(ALOAD, 0);
            constructor.visitInsn(ICONST_1);
            constructor.visitFieldInsn(PUTFIELD, internal, "firstFetch", "Z");
        }
        constructor.visitInsn(RETURN);
        constructor.visitMaxs(0, 0);
        constructor.visitEnd();
        for (int i = 0; i < methods.length; i += 2) {
            MethodVisitor mv = out.visitMethod(ACC_PUBLIC | (isStatic ? ACC_STATIC : 0), methods[i], methods[i + 1], null, null);
            mv.visitCode();
            mv.visitTypeInsn(NEW, "java/lang/AssertionError");
            mv.visitInsn(DUP);
            mv.visitLdcInsn("desktop native probe executed");
            mv.visitMethodInsn(INVOKESPECIAL, "java/lang/AssertionError", "<init>", "(Ljava/lang/Object;)V", false);
            mv.visitInsn(ATHROW);
            mv.visitMaxs(0, 0);
            mv.visitEnd();
        }
        MethodVisitor untouched = out.visitMethod(ACC_PUBLIC | ACC_STATIC, "untouched", "(I)I", null, null);
        untouched.visitCode();
        Label zero = new Label();
        untouched.visitVarInsn(ILOAD, 0);
        untouched.visitJumpInsn(IFEQ, zero);
        untouched.visitIntInsn(BIPUSH, 42);
        untouched.visitInsn(IRETURN);
        untouched.visitLabel(zero);
        untouched.visitIntInsn(BIPUSH, 7);
        untouched.visitInsn(IRETURN);
        untouched.visitMaxs(0, 0);
        untouched.visitEnd();
        out.visitEnd();
        return out.toByteArray();
    }

    private static void same(byte[] expected, byte[] actual) {
        check(expected == actual, "inactive/unsupported target must return the original bytes");
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static final class FixtureLoader extends ClassLoader {
        Class<?> define(String name, byte[] bytes) {
            return defineClass(name, bytes, 0, bytes.length);
        }
    }
}
