package cpw.mods.cl;

import java.util.HashSet;
import java.util.Set;
import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.FieldVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.Type;

/**
 * Narrow guards for desktop-only diagnostics/controller backends on NeoForge 1.21.1.
 * ModuleClassLoader also defines early graphics-service classes, before Mixin starts.
 * Emitted code references only java.base and the target's own fields: game modules do
 * not need an extra read edge to this patch. No renderer, GL capability, JAR-signing,
 * account, entitlement or game-session methods are modified.
 */
final class UwpNativeGuards {
    private static final String SODIUM = "net.caffeinemc.mods.sodium.client.compatibility.";
    private static final String CONTROLIFY = "dev.isxander.controlify.";
    private static final String HAL = "oshi.hardware.platform.windows.WindowsHardwareAbstractionLayer";
    private static final String REPORT = "net.minecraft.SystemReport";
    private static final String HID = CONTROLIFY + "hid.ControllerHIDService";
    private static final String SDL = CONTROLIFY + "driver.sdl.SDLNativesLoader";
    private static final String LEGACY_SDL = CONTROLIFY + "driver.sdl.SDL3NativesManager";
    private static final String FUTURE = "()Ljava/util/concurrent/CompletableFuture;";

    private UwpNativeGuards() {}

    static byte[] transform(String name, byte[] bytes) {
        if (!Boolean.getBoolean("banditvault.uwp")
                || !Boolean.getBoolean("banditvault.neoforge.nativeGuards")
                || bytes.length == 0 || !isTarget(name)) {
            return bytes;
        }

        ClassReader reader = new ClassReader(bytes);
        // Do not rewrite a class whose identity disagrees with the loader's request.
        if (!reader.getClassName().equals(name.replace('.', '/'))) {
            throw new IllegalArgumentException("UWP native guard class-name mismatch: " + name);
        }
        Set<String> instanceBooleans = new HashSet<>();
        Set<String> staticBooleans = new HashSet<>();
        reader.accept(new ClassVisitor(Opcodes.ASM9) {
            @Override
            public FieldVisitor visitField(int access, String field, String desc, String signature, Object value) {
                if ("Z".equals(desc) && (access & Opcodes.ACC_FINAL) == 0) {
                    ((access & Opcodes.ACC_STATIC) == 0 ? instanceBooleans : staticBooleans).add(field);
                }
                return null;
            }
        }, ClassReader.SKIP_CODE | ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);

        // Preserve original frames for all untouched code; replacements have no branches
        // and need no hierarchy resolution (which would recursively load mod classes).
        ClassWriter writer = new ClassWriter(reader, 0);
        int[] changed = {0};
        reader.accept(new ClassVisitor(Opcodes.ASM9, writer) {
            @Override
            public MethodVisitor visitMethod(int access, String method, String desc, String signature, String[] exceptions) {
                MethodVisitor out = super.visitMethod(access, method, desc, signature, exceptions);
                String action = actionFor(name, method, desc, (access & Opcodes.ACC_STATIC) != 0);
                if (action == null || (access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE)) != 0) {
                    return out;
                }
                if ("hid".equals(action)
                        && !(instanceBooleans.contains("disabled") && instanceBooleans.contains("firstFetch"))) {
                    System.err.println("[banditvault] UWP native guard: unsupported Controlify HID fields; not patched");
                    return out;
                }
                changed[0]++;
                out.visitCode();
                switch (action) {
                    case "empty":
                        out.visitMethodInsn(Opcodes.INVOKESTATIC, "java/util/Collections", "emptyList", "()Ljava/util/List;", false);
                        out.visitInsn(Opcodes.ARETURN);
                        break;
                    case "future":
                        out.visitFieldInsn(Opcodes.GETSTATIC, "java/lang/Boolean", "FALSE", "Ljava/lang/Boolean;");
                        out.visitMethodInsn(Opcodes.INVOKESTATIC, "java/util/concurrent/CompletableFuture", "completedFuture",
                                "(Ljava/lang/Object;)Ljava/util/concurrent/CompletableFuture;", false);
                        out.visitInsn(Opcodes.ARETURN);
                        break;
                    case "false":
                        if (SDL.equals(name) && staticBooleans.contains("hasAttemptedLoad")) {
                            out.visitInsn(Opcodes.ICONST_1);
                            out.visitFieldInsn(Opcodes.PUTSTATIC, reader.getClassName(), "hasAttemptedLoad", "Z");
                        }
                        out.visitInsn(Opcodes.ICONST_0);
                        out.visitInsn(Opcodes.IRETURN);
                        break;
                    case "hid":
                        out.visitVarInsn(Opcodes.ALOAD, 0);
                        out.visitInsn(Opcodes.ICONST_1);
                        out.visitFieldInsn(Opcodes.PUTFIELD, reader.getClassName(), "disabled", "Z");
                        out.visitVarInsn(Opcodes.ALOAD, 0);
                        out.visitInsn(Opcodes.ICONST_0);
                        out.visitFieldInsn(Opcodes.PUTFIELD, reader.getClassName(), "firstFetch", "Z");
                        out.visitInsn(Opcodes.RETURN);
                        break;
                    default:
                        out.visitInsn(Opcodes.RETURN);
                        break;
                }
                int locals = (access & Opcodes.ACC_STATIC) == 0 ? 1 : 0;
                for (Type argument : Type.getArgumentTypes(desc)) locals += argument.getSize();
                out.visitMaxs(2, locals);
                out.visitEnd();
                // Only these diagnostic/backend entry points lose their original code
                // attributes/annotations. All other members pass through unchanged.
                return null;
            }
        }, 0);
        if (changed[0] == 0) {
            System.err.println("[banditvault] UWP native guard: no supported signatures in " + name);
            return bytes;
        }
        System.err.println("[banditvault] UWP native guard: " + name + " (" + changed[0] + " methods)");
        return writer.toByteArray();
    }

    private static boolean isTarget(String name) {
        return (SODIUM + "environment.probe.GraphicsAdapterProbe").equals(name)
                || (SODIUM + "checks.ModuleScanner").equals(name)
                || HAL.equals(name) || REPORT.equals(name)
                || (CONTROLIFY + "Controlify").equals(name)
                || SDL.equals(name) || LEGACY_SDL.equals(name) || HID.equals(name);
    }

    private static String actionFor(String owner, String method, String desc, boolean isStatic) {
        if ((SODIUM + "environment.probe.GraphicsAdapterProbe").equals(owner) && isStatic) {
            if ("findAdapters".equals(method) && "()V".equals(desc)) return "noop";
            if ("getAdapters".equals(method) && "()Ljava/util/Collection;".equals(desc)) return "empty";
        } else if ((SODIUM + "checks.ModuleScanner").equals(owner) && isStatic) {
            if ("listModules".equals(method) && "()Ljava/util/List;".equals(desc)) return "empty";
        } else if (HAL.equals(owner) && !isStatic) {
            // Guard the caller, before WindowsGraphicsCard's native static initializer.
            if ("getGraphicsCards".equals(method) && "()Ljava/util/List;".equals(desc)) return "empty";
        } else if (REPORT.equals(owner) && !isStatic) {
            // The crash-report hardware inventory also probes CPU/WMI. Do not change
            // the real GL renderer/capability strings or any other report fields.
            if ("putHardware".equals(method) && "(Loshi/SystemInfo;)V".equals(desc)) return "noop";
        } else if ((CONTROLIFY + "Controlify").equals(owner) && !isStatic) {
            if ("askNatives".equals(method) && FUTURE.equals(desc)) return "future";
        } else if (SDL.equals(owner) && isStatic) {
            if ("tryLoad".equals(method) && "()Z".equals(desc)) return "false";
        } else if (LEGACY_SDL.equals(owner) && isStatic) {
            if ("maybeLoad".equals(method) && FUTURE.equals(desc)) return "future";
            if ("tryOfflineLoadAndStart".equals(method) && "()Z".equals(desc)) return "false";
        } else if (HID.equals(owner) && !isStatic) {
            if ("start".equals(method) && "()V".equals(desc)) return "hid";
        }
        return null;
    }
}
