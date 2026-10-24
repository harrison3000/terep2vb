// WebGL 2 for terep2.wasm's OpenGL renderer: the imports of its module "gl"
// (host_web/rz/rz_gl.h), each the WebGL call of the same name, on a canvas.  The
// module names WebGL's objects (textures, buffers...) by numbers, as OpenGL
// does, kept here with the objects; pointers are offsets in its memory.

// The imports for a canvas; getMemory: the module's memory (known only once
// it is made).
export function webglImports(canvas, getMemory) {
    let gl = null;
    const objects = new Map();      // the module's number -> { obj, uniforms (programs) }
    let next = 1;
    const locations = [];           // uniform locations: the module's number is the index

    const buffer = () => getMemory().buffer;
    const i32 = () => new Int32Array(buffer());
    const decoder = new TextDecoder(), encoder = new TextEncoder();
    const cstr = (p) => { const m = new Uint8Array(buffer()); let e = p; while (m[e]) e++; return decoder.decode(m.subarray(p, e)); };
    const obj = (n) => (n ? objects.get(n)?.obj ?? null : null);

    function generate(n, p, create) {
        const m = i32();
        for (let i = 0; i < n; i++) {
            objects.set(next, { obj: create() });
            m[(p >> 2) + i] = next++;
        }
    }
    function remove(n, p, destroy) {
        const m = i32();
        for (let i = 0; i < n; i++) {
            const id = m[(p >> 2) + i];
            if (objects.has(id)) {
                destroy(objects.get(id).obj);
                objects.delete(id);
            }
        }
    }
    function create(object) {
        objects.set(next, { obj: object });
        return next++;
    }
    // The memory as an array of the pixels' type, and where p is in it (WebGL
    // reads from there what the size, format and type need)
    function pixels(type, p) {
        switch (type) {
        case gl.FLOAT: return [new Float32Array(buffer()), p >> 2];
        case gl.UNSIGNED_SHORT: return [new Uint16Array(buffer()), p >> 1];
        case gl.UNSIGNED_INT: return [new Uint32Array(buffer()), p >> 2];
        default: return [new Uint8Array(buffer()), p];
        }
    }
    // A string into the memory at p, at most max bytes with the 0
    function putString(text, max, lenp, p) {
        const b = encoder.encode(text || '').subarray(0, Math.max(0, max - 1));
        if (p && max > 0) {
            const m = new Uint8Array(buffer());
            m.set(b, p);
            m[p + b.length] = 0;
        }
        if (lenp)
            i32()[lenp >> 2] = b.length;
    }
    const parameter = (get) => (n, name, p) => { i32()[p >> 2] = +get(obj(n), name); };

    return {
        canvasContext() {
            gl ??= canvas.getContext('webgl2', { alpha: false, depth: true, stencil: false, antialias: false });
            return gl ? 1 : 0;
        },

        glGetError: () => gl.getError(),
        glGetIntegerv: (name, p) => { i32()[p >> 2] = +gl.getParameter(name); },
        glBlendFunc: (src, dst) => gl.blendFunc(src, dst),
        glColorMask: (r, g, b, a) => gl.colorMask(!!r, !!g, !!b, !!a),
        glDepthMask: (flag) => gl.depthMask(!!flag),
        glPolygonOffset: (factor, units) => gl.polygonOffset(factor, units),
        glViewport: (x, y, w, h) => gl.viewport(x, y, w, h),
        glScissor: (x, y, w, h) => gl.scissor(x, y, w, h),
        glClearColor: (r, g, b, a) => gl.clearColor(r, g, b, a),
        glClear: (mask) => gl.clear(mask),
        glEnable: (cap) => gl.enable(cap),
        glDisable: (cap) => gl.disable(cap),
        glCullFace: (mode) => gl.cullFace(mode),
        glFrontFace: (mode) => gl.frontFace(mode),
        glDepthFunc: (func) => gl.depthFunc(func),
        glPixelStorei: (name, v) => gl.pixelStorei(name, v),

        glGenTextures: (n, p) => generate(n, p, () => gl.createTexture()),
        glDeleteTextures: (n, p) => remove(n, p, (t) => gl.deleteTexture(t)),
        glBindTexture: (target, n) => gl.bindTexture(target, obj(n)),
        glActiveTexture: (unit) => gl.activeTexture(unit),
        glTexParameteri: (target, name, v) => gl.texParameteri(target, name, v),
        glTexImage2D(target, level, internal, w, h, border, format, type, p) {
            if (p)
                gl.texImage2D(target, level, internal, w, h, border, format, type, ...pixels(type, p));
            else
                gl.texImage2D(target, level, internal, w, h, border, format, type, null);
        },
        glTexSubImage2D: (target, level, x, y, w, h, format, type, p) =>
            gl.texSubImage2D(target, level, x, y, w, h, format, type, ...pixels(type, p)),
        glTexImage3D(target, level, internal, w, h, d, border, format, type, p) {
            if (p)
                gl.texImage3D(target, level, internal, w, h, d, border, format, type, ...pixels(type, p));
            else
                gl.texImage3D(target, level, internal, w, h, d, border, format, type, null);
        },

        glGenFramebuffers: (n, p) => generate(n, p, () => gl.createFramebuffer()),
        glDeleteFramebuffers: (n, p) => remove(n, p, (f) => gl.deleteFramebuffer(f)),
        glBindFramebuffer: (target, n) => gl.bindFramebuffer(target, obj(n)),
        glCheckFramebufferStatus: (target) => gl.checkFramebufferStatus(target),
        glFramebufferTexture2D: (target, attachment, textarget, n, level) =>
            gl.framebufferTexture2D(target, attachment, textarget, obj(n), level),

        glCreateShader: (type) => create(gl.createShader(type)),
        glDeleteShader: (n) => { gl.deleteShader(obj(n)); objects.delete(n); },
        glShaderSource(n, count, srcs, lens) {
            const m = i32();
            let text = '';
            for (let i = 0; i < count; i++) {
                const p = m[(srcs >> 2) + i], len = lens ? m[(lens >> 2) + i] : -1;
                text += len >= 0 ? decoder.decode(new Uint8Array(buffer(), p, len)) : cstr(p);
            }
            gl.shaderSource(obj(n), text);
        },
        glCompileShader: (n) => gl.compileShader(obj(n)),
        glGetShaderiv: parameter((s, name) => gl.getShaderParameter(s, name)),
        glGetShaderInfoLog: (n, max, lenp, p) => putString(gl.getShaderInfoLog(obj(n)), max, lenp, p),
        glCreateProgram: () => create(gl.createProgram()),
        glDeleteProgram: (n) => { gl.deleteProgram(obj(n)); objects.delete(n); },
        glAttachShader: (program, shader) => gl.attachShader(obj(program), obj(shader)),
        glLinkProgram: (n) => gl.linkProgram(obj(n)),
        glGetProgramiv: parameter((p, name) => gl.getProgramParameter(p, name)),
        glGetProgramInfoLog: (n, max, lenp, p) => putString(gl.getProgramInfoLog(obj(n)), max, lenp, p),
        glUseProgram: (n) => gl.useProgram(obj(n)),
        glGetUniformLocation(n, name) {    // the same number for the same uniform, every time
            const program = objects.get(n), key = cstr(name);
            if (!program)
                return -1;
            program.uniforms ??= new Map();
            if (!program.uniforms.has(key)) {
                const loc = gl.getUniformLocation(program.obj, key);
                program.uniforms.set(key, loc ? locations.push(loc) - 1 : -1);
            }
            return program.uniforms.get(key);
        },
        glUniform1i: (loc, v) => gl.uniform1i(locations[loc] ?? null, v),
        glUniform1f: (loc, v) => gl.uniform1f(locations[loc] ?? null, v),
        glUniform3f: (loc, x, y, z) => gl.uniform3f(locations[loc] ?? null, x, y, z),
        glUniformMatrix4fv: (loc, count, transpose, p) =>
            gl.uniformMatrix4fv(locations[loc] ?? null, !!transpose, new Float32Array(buffer()), p >> 2, count * 16),

        glGenVertexArrays: (n, p) => generate(n, p, () => gl.createVertexArray()),
        glDeleteVertexArrays: (n, p) => remove(n, p, (a) => gl.deleteVertexArray(a)),
        glBindVertexArray: (n) => gl.bindVertexArray(obj(n)),
        glGenBuffers: (n, p) => generate(n, p, () => gl.createBuffer()),
        glDeleteBuffers: (n, p) => remove(n, p, (b) => gl.deleteBuffer(b)),
        glBindBuffer: (target, n) => gl.bindBuffer(target, obj(n)),
        glBufferData(target, size, p, usage) {
            if (p)
                gl.bufferData(target, new Uint8Array(buffer()), usage, p, size);
            else
                gl.bufferData(target, size, usage);
        },
        glBufferSubData: (target, offset, size, p) =>
            gl.bufferSubData(target, offset, new Uint8Array(buffer()), p, size),
        glVertexAttribPointer: (i, size, type, normalized, stride, offset) =>
            gl.vertexAttribPointer(i, size, type, !!normalized, stride, offset),
        glVertexAttribIPointer: (i, size, type, stride, offset) => gl.vertexAttribIPointer(i, size, type, stride, offset),
        glVertexAttribI4i: (i, x, y, z, w) => gl.vertexAttribI4i(i, x, y, z, w),
        glEnableVertexAttribArray: (i) => gl.enableVertexAttribArray(i),
        glDrawArrays: (mode, first, count) => gl.drawArrays(mode, first, count),
    };
}
