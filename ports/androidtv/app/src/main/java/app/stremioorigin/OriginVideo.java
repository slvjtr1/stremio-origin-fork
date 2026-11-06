package app.stremioorigin;

import android.app.Activity;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.view.SurfaceView;
import android.view.View;
import android.widget.FrameLayout;

import androidx.media3.common.AudioAttributes;
import androidx.media3.common.C;
import androidx.media3.common.ColorInfo;
import androidx.media3.common.Format;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.TrackSelectionOverride;
import androidx.media3.common.Tracks;
import androidx.media3.common.VideoSize;
import androidx.media3.datasource.DefaultDataSource;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;

import java.util.ArrayList;
import java.util.Locale;

/* The native side's video (src/platform/android/video_android.c). Every call comes from SDL's thread and runs on the
   main looper; what the native side reads back is kept in volatile fields, updated from the player.
   - Sound: Dolby Digital, Dolby Digital Plus (with Atmos), DTS and TrueHD go to the TV or the receiver as they are
     when they take them (ExoPlayer reads the HDMI's capabilities); otherwise the TV's own decoders.
   - The picture's frame rate: the display follows the film's when Android TV's "Match content frame rate" is on.
   - Subtitles are the app's own: ExoPlayer's text tracks are switched off. */
public final class OriginVideo {
    private static final Handler ui = new Handler(Looper.getMainLooper());
    private static Activity act; private static SurfaceView view; private static Box box; private static ExoPlayer p;
    private static volatile int st, vw, vh, audCur = -1, unsup; private static volatile long pos, dur, buf;
    private static volatile String err = "", tracks = "", hdr = ""; private static volatile float fps;
    private static final ArrayList<Tracks.Group> audGroups = new ArrayList<>(); private static final ArrayList<Integer> audIdx = new ArrayList<>(); /* (main looper only) */

    /* the picture's place: any rectangle, also one larger than the screen (Fill and Zoom) */
    static final class Box extends FrameLayout {
        int x, y, w = -1, h = -1;
        Box(Context c) { super(c); setClipChildren(true); }
        @Override protected void onMeasure(int ws, int hs) {
            super.onMeasure(ws, hs);
            int cw = w < 0 ? getMeasuredWidth() : Math.max(1, w), ch = h < 0 ? getMeasuredHeight() : Math.max(1, h);
            for (int i = 0; i < getChildCount(); i++) getChildAt(i).measure(MeasureSpec.makeMeasureSpec(cw, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(ch, MeasureSpec.EXACTLY));
        }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
            for (int i = 0; i < getChildCount(); i++) { View c = getChildAt(i); int cx = w < 0 ? 0 : x, cy = h < 0 ? 0 : y; c.layout(cx, cy, cx + c.getMeasuredWidth(), cy + c.getMeasuredHeight()); }
        }
    }

    /* v is placed by the activity; it goes inside a Box in its place */
    static void attach(Activity a, SurfaceView v) {
        act = a; View par = (View) v.getParent();
        if (par instanceof android.view.ViewGroup) {
            android.view.ViewGroup g = (android.view.ViewGroup) par; int at = g.indexOfChild(v); android.view.ViewGroup.LayoutParams lp = v.getLayoutParams();
            g.removeView(v); box = new Box(a); box.addView(v, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)); g.addView(box, at, lp);
        }
        view = v; v.setVisibility(View.INVISIBLE);
    }
    static void release() { ui.post(() -> { if (p != null) { p.release(); p = null; } st = 0; }); }
    static void backgrounded() { if (p != null && p.getPlayWhenReady()) p.setPlayWhenReady(false); } /* Home: the film waits (the app saves where it was) */

    private static void ensure() {
        if (p != null || act == null) return;
        DefaultHttpDataSource.Factory http = new DefaultHttpDataSource.Factory().setAllowCrossProtocolRedirects(true)
            .setConnectTimeoutMs(20000).setReadTimeoutMs(30000).setUserAgent("StremioOrigin/" + BuildConfig.VERSION_NAME + " (Android TV)");
        DefaultRenderersFactory rf = new DefaultRenderersFactory(act).setEnableDecoderFallback(true);
        p = new ExoPlayer.Builder(act, rf).setMediaSourceFactory(new DefaultMediaSourceFactory(new DefaultDataSource.Factory(act, http)))
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(), true)
            .setHandleAudioBecomingNoisy(true).build();
        p.setVideoChangeFrameRateStrategy(C.VIDEO_CHANGE_FRAME_RATE_STRATEGY_ONLY_IF_SEAMLESS);
        p.setTrackSelectionParameters(p.getTrackSelectionParameters().buildUpon().setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true).build());
        p.setVideoSurfaceView(view);
        p.addListener(new Player.Listener() {
            @Override public void onPlaybackStateChanged(int s) { sync(); }
            @Override public void onIsPlayingChanged(boolean b) { sync(); }
            @Override public void onVideoSizeChanged(VideoSize s) { vw = s.width; vh = s.height; video(); }
            @Override public void onTracksChanged(Tracks t) { listTracks(t); video(); }
            @Override public void onPlayerError(PlaybackException e) { err = e.getErrorCodeName(); st = 6; }
        });
        ui.post(OriginVideo::tick);
    }
    private static void sync() {
        if (p == null) { st = 0; return; }
        if (st == 6) return;
        switch (p.getPlaybackState()) {
            case Player.STATE_IDLE: st = 0; break;
            case Player.STATE_BUFFERING: st = vw > 0 ? 4 : 1; break;
            case Player.STATE_READY: st = p.getPlayWhenReady() ? 2 : 3; break;
            case Player.STATE_ENDED: st = 5; break;
        }
    }
    /* the position and the buffer, 4 times a second, as long as a player exists */
    private static void tick() {
        if (p == null) return;
        pos = p.getCurrentPosition(); long d = p.getDuration(); dur = d > 0 ? d : 0; buf = p.getBufferedPosition(); sync();
        ui.postDelayed(OriginVideo::tick, 250);
    }
    private static String codec(String m) {
        if (m == null) return "";
        switch (m) {
            case MimeTypes.AUDIO_AC3: return "ac3";
            case MimeTypes.AUDIO_E_AC3: case MimeTypes.AUDIO_E_AC3_JOC: return "eac3";
            case MimeTypes.AUDIO_AC4: return "ac4";
            case MimeTypes.AUDIO_DTS: case MimeTypes.AUDIO_DTS_EXPRESS: return "dts";
            case MimeTypes.AUDIO_DTS_HD: return "dts-hd";
            case MimeTypes.AUDIO_DTS_X: return "dts-x";
            case MimeTypes.AUDIO_TRUEHD: return "truehd";
            case MimeTypes.AUDIO_AAC: return "aac";
            case MimeTypes.AUDIO_MPEG: return "mp3";
            case MimeTypes.AUDIO_OPUS: return "opus";
            case MimeTypes.AUDIO_VORBIS: return "vorbis";
            case MimeTypes.AUDIO_FLAC: return "flac";
            default: return m.startsWith("audio/") ? m.substring(6) : m;
        }
    }
    private static String lang3(String l) {
        if (l == null || l.isEmpty() || "und".equals(l)) return "";
        try { String s = Locale.forLanguageTag(l).getISO3Language(); return s.isEmpty() ? l : s; } catch (Exception e) { return l; }
    }
    private static String clean(String s) { return s == null ? "" : s.replace('\t', ' ').replace('\n', ' '); }
    /* the audio tracks as lines for the native side: "idx\tlang\tname\tcodec\tchannels\tforced" */
    private static void listTracks(Tracks t) {
        audGroups.clear(); audIdx.clear(); StringBuilder sb = new StringBuilder(); int cur = -1, n = 0, playable = 0, any = 0;
        for (Tracks.Group g : t.getGroups()) {
            if (g.getType() != C.TRACK_TYPE_AUDIO) continue;
            for (int i = 0; i < g.length; i++) {
                Format f = g.getTrackFormat(i); any++; if (g.isTrackSupported(i)) playable++;
                if (g.isTrackSelected(i)) cur = n;
                sb.append(n).append('\t').append(lang3(f.language)).append('\t').append(clean(f.label)).append('\t').append(codec(f.sampleMimeType)).append('\t')
                  .append(f.channelCount > 0 ? f.channelCount : 0).append('\t').append((f.selectionFlags & C.SELECTION_FLAG_FORCED) != 0 ? 1 : 0).append('\n');
                audGroups.add(g); audIdx.add(i); n++;
            }
        }
        tracks = sb.toString(); audCur = cur; unsup = any > 0 && (playable == 0 || cur < 0) ? 1 : 0;
    }
    /* the frame rate and the HDR kind of the picture playing */
    private static void video() {
        if (p == null) return; Format f = p.getVideoFormat(); if (f == null) return;
        fps = f.frameRate > 0 ? f.frameRate : 0;
        String h = "none"; ColorInfo c = f.colorInfo;
        if (MimeTypes.VIDEO_DOLBY_VISION.equals(f.sampleMimeType) || (f.codecs != null && (f.codecs.startsWith("dvh") || f.codecs.startsWith("dva")))) h = "DolbyVision";
        else if (c != null && c.colorTransfer == C.COLOR_TRANSFER_ST2084) h = "HDR10";
        else if (c != null && c.colorTransfer == C.COLOR_TRANSFER_HLG) h = "HLG";
        hdr = h;
    }

    public static void load(String url) { ui.post(() -> { ensure(); if (p == null) return; err = ""; st = 1; vw = vh = 0; fps = 0; hdr = ""; tracks = ""; audCur = -1; unsup = 0; view.setVisibility(View.VISIBLE);
        p.setTrackSelectionParameters(p.getTrackSelectionParameters().buildUpon().clearOverridesOfType(C.TRACK_TYPE_AUDIO).build());
        MediaItem.Builder mi = new MediaItem.Builder().setUri(url); String u = url.toLowerCase(Locale.ROOT);
        if (u.contains(".m3u8")) mi.setMimeType(MimeTypes.APPLICATION_M3U8); else if (u.contains(".mpd")) mi.setMimeType(MimeTypes.APPLICATION_MPD);
        p.setMediaItem(mi.build()); p.prepare(); p.setPlayWhenReady(true); }); }
    public static void unload() { ui.post(() -> { if (p != null) { p.stop(); p.clearMediaItems(); } st = 0; vw = vh = 0; pos = dur = buf = 0; tracks = ""; audCur = -1; unsup = 0; if (view != null) view.setVisibility(View.INVISIBLE); }); }
    public static void play() { ui.post(() -> { if (p != null) p.setPlayWhenReady(true); }); }
    public static void pause() { ui.post(() -> { if (p != null) p.setPlayWhenReady(false); }); }
    public static void seek(long ms) { pos = ms; ui.post(() -> { if (p != null) p.seekTo(ms); }); }
    public static void volume(float v) { ui.post(() -> { if (p != null) p.setVolume(v); }); }
    public static void selectAudio(int idx) { audCur = idx; ui.post(() -> { if (p == null || idx < 0 || idx >= audGroups.size()) return; Tracks.Group g = audGroups.get(idx);
        p.setTrackSelectionParameters(p.getTrackSelectionParameters().buildUpon().setOverrideForType(new TrackSelectionOverride(g.getMediaTrackGroup(), audIdx.get(idx))).build()); }); }
    /* fractions of the screen: the picture's rectangle (larger than the screen for Fill and Zoom) */
    public static void window(float x, float y, float w, float h) {
        ui.post(() -> { if (box == null) return; View par = (View) box.getParent(); if (par == null) return;
            int W = par.getWidth(), H = par.getHeight(); if (W <= 0 || H <= 0) return;
            box.x = Math.round(x * W); box.y = Math.round(y * H); box.w = Math.max(1, Math.round(w * W)); box.h = Math.max(1, Math.round(h * H)); box.requestLayout(); });
    }
    public static long position() { return pos; }
    public static long duration() { return dur; }
    public static long buffered() { return buf; }
    public static int state() { return st; }
    public static int width() { return vw; }
    public static int height() { return vh; }
    public static String error() { return err; }
    public static String audioTracks() { return tracks; }
    public static int audioCurrent() { return audCur; }
    public static int audioUnsupported() { return unsup; }
    public static float fps() { return fps; }
    public static String hdr() { return hdr; }
    private OriginVideo() {}
}
