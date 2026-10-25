// The sound's AudioWorklet (see terep2.js): plays the samples the game's
// module makes as the time advances, queued; drops the oldest past a quarter
// of a second (the page's clock and the sound card's drift apart), plays
// silence when none are left.  Messages: Float32Array, stereo interleaved;
// null drops all those queued (the game stopped).
class OplPlayer extends AudioWorkletProcessor {
    constructor() {
        super();
        this.queue = [];
        this.pos = 0;
        this.port.onmessage = (e) => {
            if (!e.data) {
                this.queue = [];
                this.pos = 0;
                return;
            }
            this.queue.push(e.data);
            let frames = -this.pos / 2;
            for (const b of this.queue) frames += b.length / 2;
            while (frames > sampleRate / 4 && this.queue.length > 1) {
                frames -= (this.queue.shift().length - this.pos) / 2;
                this.pos = 0;
            }
        };
    }
    process(inputs, outputs) {
        const [l, r] = outputs[0];
        for (let i = 0; i < l.length; i++) {
            const b = this.queue[0];
            if (!b) { l[i] = r[i] = 0; continue; }
            l[i] = b[this.pos];
            r[i] = b[this.pos + 1];
            this.pos += 2;
            if (this.pos >= b.length) { this.queue.shift(); this.pos = 0; }
        }
        return true;
    }
}
registerProcessor('opl-player', OplPlayer);
