// The OpenGL 3.3 core functions the renderizeitor library uses (its rz_gl.h),
// done with WebGL 2 on a canvas: the imports of terep2.wasm's module "gl"
// (see rz_platform_web.cpp).  The library's numbers for objects (textures,
// buffers...) are kept here, each with its WebGL object; pointers are offsets
// in the module's memory.
//
// What WebGL 2 does not have, done here so the library stays as it is:
//   texture buffers (samplerBuffer): a 2D texture, TBO_WIDTH texels a row,
//     kept with the buffer's bytes, which stay here (they are small: a car's
//     vertices and its polygons' indices); in the shaders, texelFetch on one
//     becomes rzFetch, which finds the texel in the rows;
//   the BGRA format: the bytes are put in RGBA order on the way;
//   GLSL 3.30 core: the shaders become GLSL ES 3.00 (toEs);
//   glDrawBuffer: glDrawBuffers of one.

const GL_TEXTURE_BUFFER = 0x8c2a, GL_TEXTURE_2D = 0x0de1, GL_TEXTURE0 = 0x84c0, GL_BGRA = 0x80e1;
const GL_RGBA = 0x1908, GL_RGBA32F = 0x8814, GL_R16UI = 0x8234;
const GL_COMPILE_STATUS = 0x8b81, GL_LINK_STATUS = 0x8b82, GL_INFO_LOG_LENGTH = 0x8b84;
const TBO_WIDTH = 1024;             // texels a row of a texture buffer's texture (and in rzFetch)

// A shader of the library (GLSL 3.30 core) in GLSL ES 3.00, for WebGL 2.
export function toEs(source) {
    let s = source.replace(/^\s*#version\s+330\s+core\s*$/m, '');
    const buffers = [...s.matchAll(/\b(u?)samplerBuffer\s+(\w+)/g)].map((m) => m[2]);
    s = s.replace(/\b(u?)samplerBuffer\b/g, '$1sampler2D');
    for (const name of buffers)
        s = s.replace(new RegExp(`\\btexelFetch\\(\\s*${name}\\s*,`, 'g'), `rzFetch(${name},`);
    let head = '#version 300 es\n' +
        'precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n' +
        'precision highp usampler2D;\nprecision highp sampler2DArray;\nprecision highp sampler2DShadow;\n';
    if (buffers.length)
        head += `vec4 rzFetch(sampler2D s, int i) { return texelFetch(s, ivec2(i % ${TBO_WIDTH}, i / ${TBO_WIDTH}), 0); }\n` +
            `uvec4 rzFetch(usampler2D s, int i) { return texelFetch(s, ivec2(i % ${TBO_WIDTH}, i / ${TBO_WIDTH}), 0); }\n`;
    return head + '#line 2\n' + s;
}

// Bytes a pixel, by format and type (those the library uses, and a few more).
const CHANNELS = { 0x1903: 1, 0x8d94: 1, 0x1902: 1, 0x8227: 2, 0x1907: 3, 0x1908: 4, 0x80e1: 4 };
const TYPE_SIZE = { 0x1400: 1, 0x1401: 1, 0x1402: 2, 0x1403: 2, 0x1404: 4, 0x1405: 4, 0x1406: 4, 0x140b: 2 };

// The imports of the module "gl" for a canvas; getMemory: the module's memory
// (known only once it is made).
export function webglImports(canvas, getMemory) {
    let gl = null;
    const names = new Map();        // library's number -> record ({ obj, ... })
    let next = 1;
    const locations = [];           // uniform locations: the library's number is the index
    let unpackAlignment = 4;
    let unit = 0;                   // active texture unit
    // On each unit: the texture bound to GL_TEXTURE_2D, to GL_TEXTURE_BUFFER, and
    // the one WebGL's TEXTURE_2D has (the last bound; a texture buffer is a 2D one there)
    const bound2d = [], boundBuffer = [], bound = [];
    let boundTextureBuffer = 0;     // the buffer bound to GL_TEXTURE_BUFFER

    const u8 = () => new Uint8Array(getMemory().buffer);
    const i32 = () => new Int32Array(getMemory().buffer);
    const decoder = new TextDecoder(), encoder = new TextEncoder();
    const cstr = (p) => { const m = u8(); let e = p; while (m[e]) e++; return decoder.decode(m.subarray(p, e)); };
    const obj = (n) => (n ? names.get(n)?.obj ?? null : null);

    function generate(n, p, make) {
        const m = i32();
        for (let i = 0; i < n; i++) {
            const id = next++;
            names.set(id, make(id));
            m[(p >> 2) + i] = id;
        }
    }
    function remove(n, p, drop) {
        const m = i32();
        for (let i = 0; i < n; i++) {
            const id = m[(p >> 2) + i], r = names.get(id);
            if (r) {
                drop(r);
                names.delete(id);
            }
        }
    }
    // A string into the memory at p, at most max bytes with the 0.
    function putString(text, max, lenp, p) {
        const b = encoder.encode(text).subarray(0, Math.max(0, max - 1));
        if (p && max > 0) {
            u8().set(b, p);
            u8()[p + b.length] = 0;
        }
        if (lenp)
            i32()[lenp >> 2] = b.length;
    }

    // The pixels at p for glTexImage*: a view of the memory, of the type's
    // array, or null; in RGBA order if they are BGRA.
    function pixels(w, h, d, format, type, p) {
        if (!p)
            return null;
        const bpp = (CHANNELS[format] || 4) * (TYPE_SIZE[type] || 1);
        const row = Math.ceil(w * bpp / unpackAlignment) * unpackAlignment;
        const bytes = row * (h * d - 1) + w * bpp;
        let view = u8().subarray(p, p + bytes);
        if (format === GL_BGRA) {
            view = view.slice();
            for (let i = 0; i + 3 < view.length; i += 4) {
                const b = view[i];
                view[i] = view[i + 2];
                view[i + 2] = b;
            }
        }
        const buf = view.buffer, off = view.byteOffset;
        switch (TYPE_SIZE[type] === 4 ? type : 0) {
        case 0x1406: return new Float32Array(buf, off, bytes >> 2);
        case 0x1404: return new Int32Array(buf, off, bytes >> 2);
        case 0x1405: return new Uint32Array(buf, off, bytes >> 2);
        }
        if (TYPE_SIZE[type] === 2)
            return type === 0x1402 ? new Int16Array(buf, off, bytes >> 1) : new Uint16Array(buf, off, bytes >> 1);
        return type === 0x1400 ? new Int8Array(buf, off, bytes) : view;
    }

    // A texture buffer's texture again from its buffer's bytes.
    function uploadTextureBuffer(t) {
        const b = names.get(t.buffer);
        if (!b || !b.data)
            return;
        const float = t.format === GL_RGBA32F, size = float ? 16 : 2;
        const count = Math.max(1, Math.floor(b.data.length / size));
        const w = Math.min(count, TBO_WIDTH), h = Math.ceil(count / w);
        const bytes = new Uint8Array(w * h * size);
        bytes.set(b.data.subarray(0, Math.min(b.data.length, bytes.length)));
        gl.bindTexture(gl.TEXTURE_2D, t.obj);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
        if (float)
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, w, h, 0, gl.RGBA, gl.FLOAT, new Float32Array(bytes.buffer));
        else
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.R16UI, w, h, 0, gl.RED_INTEGER, gl.UNSIGNED_SHORT, new Uint16Array(bytes.buffer));
        if (!t.ready) {
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
            t.ready = true;
        }
        gl.pixelStorei(gl.UNPACK_ALIGNMENT, unpackAlignment);
        gl.bindTexture(gl.TEXTURE_2D, obj(bound[unit]));
    }
    function textureBufferChanged(b) {
        for (const id of b.textures) {
            const t = names.get(id);
            if (t && t.buffer === b.id)
                uploadTextureBuffer(t);
        }
    }

    function shaderParameter(get, log, statusName) {
        return (n, pname, p) => {
            const o = obj(n);
            let v = 0;
            if (pname === statusName)
                v = get(o, pname) ? 1 : 0;
            else if (pname === GL_INFO_LOG_LENGTH)
                v = (log(o) || '').length + 1;
            else
                v = +get(o, pname);
            i32()[p >> 2] = v;
        };
    }

    return {
        // the platform (rz_platform_web.cpp)
        rzweb_context() {
            if (!gl)
                gl = canvas.getContext('webgl2', { alpha: false, depth: true, stencil: false, antialias: false });
            return gl ? 1 : 0;
        },
        rzweb_size(wp, hp) {
            const m = i32();
            m[wp >> 2] = gl.drawingBufferWidth;
            m[hp >> 2] = gl.drawingBufferHeight;
        },

        glGetString: () => 0,       // not used by the library
        glGetError: () => gl.getError(),
        glGetIntegerv(pname, p) {
            const v = gl.getParameter(pname), m = i32();
            if (v && typeof v === 'object' && 'length' in v)
                for (let i = 0; i < v.length; i++)
                    m[(p >> 2) + i] = v[i];
            else
                m[p >> 2] = +v;
        },
        glDrawBuffer: (mode) => gl.drawBuffers([mode]),
        glBlendFunc: (s, d) => gl.blendFunc(s, d),
        glColorMask: (r, g, b, a) => gl.colorMask(!!r, !!g, !!b, !!a),
        glDepthMask: (f) => gl.depthMask(!!f),
        glReadBuffer: (mode) => gl.readBuffer(mode),
        glPolygonOffset: (f, u) => gl.polygonOffset(f, u),
        glFramebufferTexture2D: (target, att, textarget, t, level) =>
            gl.framebufferTexture2D(target, att, textarget, obj(t), level),
        glViewport: (x, y, w, h) => gl.viewport(x, y, w, h),
        glScissor: (x, y, w, h) => gl.scissor(x, y, w, h),
        glClearColor: (r, g, b, a) => gl.clearColor(r, g, b, a),
        glClear: (mask) => gl.clear(mask),
        glEnable: (cap) => gl.enable(cap),
        glDisable: (cap) => gl.disable(cap),
        glCullFace: (mode) => gl.cullFace(mode),
        glFrontFace: (mode) => gl.frontFace(mode),
        glDepthFunc: (f) => gl.depthFunc(f),
        glFinish: () => gl.finish(),
        glPixelStorei(pname, v) {
            if (pname === gl.UNPACK_ALIGNMENT)
                unpackAlignment = v;
            gl.pixelStorei(pname, v);
        },
        glReadPixels(x, y, w, h, format, type, p) {
            const view = u8().subarray(p, p + w * h * 4);
            gl.readPixels(x, y, w, h, GL_RGBA, type, view);
            if (format === GL_BGRA)
                for (let i = 0; i < view.length; i += 4) {
                    const b = view[i];
                    view[i] = view[i + 2];
                    view[i + 2] = b;
                }
        },

        glGenTextures: (n, p) => generate(n, p, () => ({ obj: gl.createTexture() })),
        glDeleteTextures: (n, p) => remove(n, p, (r) => gl.deleteTexture(r.obj)),
        glBindTexture(target, n) {
            if (target === GL_TEXTURE_BUFFER) {
                boundBuffer[unit] = n;
                n = bound[unit] = n || bound2d[unit];   // unbound: the 2D one is back
                target = gl.TEXTURE_2D;
            } else if (target === GL_TEXTURE_2D) {
                bound2d[unit] = bound[unit] = n;
            }
            gl.bindTexture(target, obj(n));
        },
        glActiveTexture(t) {
            unit = t - GL_TEXTURE0;
            gl.activeTexture(t);
        },
        glTexParameteri: (target, pname, v) => gl.texParameteri(target, pname, v),
        glTexParameterf: (target, pname, v) => gl.texParameterf(target, pname, v),
        glTexImage2D: (target, level, internal, w, h, border, format, type, p) =>
            gl.texImage2D(target, level, internal, w, h, border, format === GL_BGRA ? GL_RGBA : format, type,
                          pixels(w, h, 1, format, type, p)),
        glTexSubImage2D: (target, level, x, y, w, h, format, type, p) =>
            gl.texSubImage2D(target, level, x, y, w, h, format === GL_BGRA ? GL_RGBA : format, type,
                             pixels(w, h, 1, format, type, p)),
        glTexImage3D: (target, level, internal, w, h, d, border, format, type, p) =>
            gl.texImage3D(target, level, internal, w, h, d, border, format === GL_BGRA ? GL_RGBA : format, type,
                          pixels(w, h, d, format, type, p)),
        glGenerateMipmap: (target) => gl.generateMipmap(target),
        // the texture bound to GL_TEXTURE_BUFFER on this unit takes the buffer's bytes
        glTexBuffer(target, format, buffer) {
            const t = names.get(boundBuffer[unit]), b = names.get(buffer);
            if (!t || !b)
                return;
            t.buffer = buffer;
            t.format = format === GL_R16UI ? GL_R16UI : GL_RGBA32F;
            b.textures.add(boundBuffer[unit]);
            uploadTextureBuffer(t);
        },

        glGenFramebuffers: (n, p) => generate(n, p, () => ({ obj: gl.createFramebuffer() })),
        glDeleteFramebuffers: (n, p) => remove(n, p, (r) => gl.deleteFramebuffer(r.obj)),
        glBindFramebuffer: (target, n) => gl.bindFramebuffer(target, obj(n)),
        glCheckFramebufferStatus: (target) => gl.checkFramebufferStatus(target),
        glFramebufferRenderbuffer: (target, att, rbtarget, n) =>
            gl.framebufferRenderbuffer(target, att, rbtarget, obj(n)),
        glGenRenderbuffers: (n, p) => generate(n, p, () => ({ obj: gl.createRenderbuffer() })),
        glDeleteRenderbuffers: (n, p) => remove(n, p, (r) => gl.deleteRenderbuffer(r.obj)),
        glBindRenderbuffer: (target, n) => gl.bindRenderbuffer(target, obj(n)),
        glRenderbufferStorage: (target, format, w, h) => gl.renderbufferStorage(target, format, w, h),

        glCreateShader(type) {
            const id = next++;
            names.set(id, { obj: gl.createShader(type) });
            return id;
        },
        glDeleteShader: (n) => { gl.deleteShader(obj(n)); names.delete(n); },
        glShaderSource(n, count, srcs, lens) {
            const m = i32();
            let text = '';
            for (let i = 0; i < count; i++) {
                const p = m[(srcs >> 2) + i], len = lens ? m[(lens >> 2) + i] : -1;
                text += len >= 0 ? decoder.decode(u8().subarray(p, p + len)) : cstr(p);
            }
            gl.shaderSource(obj(n), toEs(text));
        },
        glCompileShader(n) {
            const s = obj(n);
            gl.compileShader(s);
            if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
                console.warn('webgl: shader:', gl.getShaderInfoLog(s), '\n' + gl.getShaderSource(s));
        },
        glGetShaderiv: shaderParameter((o, p) => gl.getShaderParameter(o, p), (o) => gl.getShaderInfoLog(o),
                                       GL_COMPILE_STATUS),
        glGetShaderInfoLog: (n, max, lenp, p) => putString(gl.getShaderInfoLog(obj(n)) || '', max, lenp, p),
        glCreateProgram() {
            const id = next++;
            names.set(id, { obj: gl.createProgram() });
            return id;
        },
        glDeleteProgram: (n) => { gl.deleteProgram(obj(n)); names.delete(n); },
        glAttachShader: (prog, sh) => gl.attachShader(obj(prog), obj(sh)),
        glLinkProgram(n) {
            const p = obj(n);
            gl.linkProgram(p);
            if (!gl.getProgramParameter(p, gl.LINK_STATUS))
                console.warn('webgl: program:', gl.getProgramInfoLog(p));
        },
        glGetProgramiv: shaderParameter((o, p) => gl.getProgramParameter(o, p), (o) => gl.getProgramInfoLog(o),
                                        GL_LINK_STATUS),
        glGetProgramInfoLog: (n, max, lenp, p) => putString(gl.getProgramInfoLog(obj(n)) || '', max, lenp, p),
        glUseProgram: (n) => gl.useProgram(obj(n)),
        glGetUniformLocation(n, name) {    // the same number for the same uniform, every time
            const prog = names.get(n), key = cstr(name);
            if (!prog)
                return -1;
            prog.uniforms ??= new Map();
            if (!prog.uniforms.has(key)) {
                const loc = gl.getUniformLocation(prog.obj, key);
                prog.uniforms.set(key, loc ? locations.push(loc) - 1 : -1);
            }
            return prog.uniforms.get(key);
        },
        glUniform1i: (loc, v) => { if (loc >= 0) gl.uniform1i(locations[loc], v); },
        glUniform1f: (loc, v) => { if (loc >= 0) gl.uniform1f(locations[loc], v); },
        glUniform3f: (loc, x, y, z) => { if (loc >= 0) gl.uniform3f(locations[loc], x, y, z); },
        glUniformMatrix4fv(loc, count, transpose, p) {
            if (loc < 0)
                return;
            let v = new Float32Array(getMemory().buffer, p, count * 16);
            if (transpose) {            // WebGL wants it as it is
                v = v.slice();
                for (let k = 0; k < count; k++)
                    for (let i = 0; i < 4; i++)
                        for (let j = i + 1; j < 4; j++) {
                            const a = 16 * k + 4 * i + j, b = 16 * k + 4 * j + i, t = v[a];
                            v[a] = v[b];
                            v[b] = t;
                        }
            }
            gl.uniformMatrix4fv(locations[loc], false, v);
        },

        glGenVertexArrays: (n, p) => generate(n, p, () => ({ obj: gl.createVertexArray() })),
        glDeleteVertexArrays: (n, p) => remove(n, p, (r) => gl.deleteVertexArray(r.obj)),
        glBindVertexArray: (n) => gl.bindVertexArray(obj(n)),
        // a buffer gets its WebGL buffer when first bound as one; bound to
        // GL_TEXTURE_BUFFER, its bytes stay here
        glGenBuffers: (n, p) => generate(n, p, (id) => ({ id, obj: null, data: null, textures: new Set() })),
        glDeleteBuffers: (n, p) => remove(n, p, (r) => { if (r.obj) gl.deleteBuffer(r.obj); }),
        glBindBuffer(target, n) {
            if (target === GL_TEXTURE_BUFFER) {
                boundTextureBuffer = n;
                return;
            }
            const b = names.get(n);
            if (b && !b.obj)
                b.obj = gl.createBuffer();
            gl.bindBuffer(target, b ? b.obj : null);
        },
        glBufferData(target, size, p, usage) {
            if (target === GL_TEXTURE_BUFFER) {
                const b = names.get(boundTextureBuffer);
                if (!b)
                    return;
                b.data = p ? u8().slice(p, p + size) : new Uint8Array(size);
                textureBufferChanged(b);
                return;
            }
            if (p)
                gl.bufferData(target, u8().subarray(p, p + size), usage);
            else
                gl.bufferData(target, size, usage);
        },
        glBufferSubData(target, offset, size, p) {
            if (target === GL_TEXTURE_BUFFER) {
                const b = names.get(boundTextureBuffer);
                if (!b || !b.data)
                    return;
                b.data.set(u8().subarray(p, p + Math.min(size, b.data.length - offset)), offset);
                textureBufferChanged(b);
                return;
            }
            gl.bufferSubData(target, offset, u8().subarray(p, p + size));
        },
        glVertexAttribPointer: (i, size, type, norm, stride, offset) =>
            gl.vertexAttribPointer(i, size, type, !!norm, stride, offset),
        glVertexAttribIPointer: (i, size, type, stride, offset) => gl.vertexAttribIPointer(i, size, type, stride, offset),
        glEnableVertexAttribArray: (i) => gl.enableVertexAttribArray(i),
        glDrawArrays: (mode, first, count) => gl.drawArrays(mode, first, count),
    };
}
