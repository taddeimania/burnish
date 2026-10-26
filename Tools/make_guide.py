"""make_guide.py - builds docs/Burnish-Guide.pdf.

    python3 Tools/make_guide.py docs/Burnish-Guide.pdf <screenshot-folder>

The screenshot folder holds PNGs made with BurnishSnapshot (build with
-DBURNISH_BUILD_TOOLS=ON), run from the build folder:

    S=./BurnishSnapshot_artefacts/Release/BurnishSnapshot
    $S shots/overview.png 1                 # Clean Master, Equalizer 1 showing
    for id in $(seq 0 17); do $S shots/mod$id.png 0 $id; done
    $S shots/assistrun.png assistrun        # the assistant's result page
    $S shots/dither.png dither

Needs reportlab and Pillow. Same page style as the Lacquer guide.
"""
import os
import sys

from PIL import Image as PILImage
from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.platypus import (BaseDocTemplate, Flowable, Frame, Image, KeepTogether, ListFlowable,
                                ListItem, PageBreak, PageTemplate, Paragraph, Spacer, Table, TableStyle)
from reportlab.platypus.doctemplate import NextPageTemplate

OUT, SHOTS = sys.argv[1], sys.argv[2]
TMP = os.path.join(SHOTS, "_cropped")
os.makedirs(TMP, exist_ok=True)

PANEL = colors.HexColor("#2a2a2c")
BRAND = colors.HexColor("#e2a84c")
BRAND_DARK = colors.HexColor("#b07a1e")
CREAM = colors.HexColor("#f3f0e6")
INK = colors.HexColor("#1c1c1e")
DIM = colors.HexColor("#6b6b6b")
RULE = colors.HexColor("#d6d6d6")
TINT = colors.HexColor("#fbf2e2")

W, H = letter
M = 0.9 * inch
FW = W - 2 * M

body = ParagraphStyle("body", fontName="Helvetica", fontSize=9.8, leading=13.6, textColor=INK, spaceAfter=6)
lead = ParagraphStyle("lead", parent=body, fontSize=11, leading=15.5, spaceAfter=8)
h1 = ParagraphStyle("h1", parent=body, fontName="Helvetica-Bold", fontSize=19, leading=23, spaceBefore=4, spaceAfter=10)
h2 = ParagraphStyle("h2", parent=body, fontName="Helvetica-Bold", fontSize=13, leading=16, spaceBefore=12, spaceAfter=5)
h3 = ParagraphStyle("h3", parent=body, fontName="Helvetica-Bold", fontSize=10.5, leading=13.5, spaceBefore=8, spaceAfter=3)
small = ParagraphStyle("small", parent=body, fontSize=8.3, leading=11, textColor=DIM)
cell = ParagraphStyle("cell", parent=body, fontSize=8.8, leading=11.6, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Helvetica-Bold")
callout = ParagraphStyle("callout", parent=body, fontSize=9.3, leading=12.8, backColor=TINT, borderPadding=(8, 10, 8, 12),
                         spaceBefore=10, spaceAfter=14, leftIndent=4, rightIndent=4)


def P(t, s=body):
    return Paragraph(t, s)


def bullets(items, style=body):
    return ListFlowable([ListItem(P(i, style), leftIndent=12, value="•") for i in items],
                        bulletType="bullet", start="•", leftIndent=12, bulletFontSize=8)


def table(rows, widths, header=True, bold_first=True):
    data = []
    for r, row in enumerate(rows):
        out = []
        for c, text in enumerate(row):
            st = cellb if (header and r == 0) or (bold_first and c == 0) else cell
            out.append(P(text, st))
        data.append(out)
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    st = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
          ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 4), ("LEFTPADDING", (0, 0), (-1, -1), 3)]
    if header:
        st.append(("LINEBELOW", (0, 0), (-1, 0), 0.9, INK))
    t.setStyle(TableStyle(st))
    return t


class Note(Flowable):
    """The tinted callout with a brand bar down its left edge, as in the Lacquer guide."""

    def __init__(self, html):
        super().__init__()
        self.p = P(html, ParagraphStyle("n", parent=body, fontSize=9.3, leading=12.8, spaceAfter=0))

    def wrap(self, aw, ah):
        self.w = aw
        _, self.ph = self.p.wrap(aw - 26, ah)
        self.h = self.ph + 20
        return aw, self.h + 10

    def draw(self):
        c = self.canv
        c.setFillColor(TINT)
        c.rect(0, 0, self.w, self.h, stroke=0, fill=1)
        c.setFillColor(BRAND)
        c.rect(0, 0, 3, self.h, stroke=0, fill=1)
        self.p.drawOn(c, 16, 10)


def crop(name, box, out):
    path = os.path.join(TMP, out)
    PILImage.open(os.path.join(SHOTS, name)).convert("RGB").crop(box).save(path, quality=90)
    return path


def image(path, width):
    w, h = PILImage.open(path).size
    return Image(path, width=width, height=width * h / w)


class Annotated(Flowable):
    """A screenshot with numbered markers on it."""

    def __init__(self, path, width, marks, src_size):
        super().__init__()
        self.path, self.width_, self.marks, self.src = path, width, marks, src_size
        self.height_ = width * src_size[1] / src_size[0]

    def wrap(self, aw, ah):
        return self.width_, self.height_ + 4

    def draw(self):
        c = self.canv
        c.drawImage(self.path, 0, 4, width=self.width_, height=self.height_)
        k = self.width_ / self.src[0]
        for n, (x, y) in enumerate(self.marks, start=1):
            px, py = x * k, 4 + self.height_ - y * k
            c.setFillColor(BRAND)
            c.setStrokeColor(colors.white)
            c.setLineWidth(1)
            c.circle(px, py, 7.5, stroke=1, fill=1)
            c.setFillColor(INK)
            c.setFont("Helvetica-Bold", 8)
            c.drawCentredString(px, py - 2.8, str(n))


# ---------------------------------------------------------------------------
# Page furniture
# ---------------------------------------------------------------------------
def footer(c, doc):
    c.saveState()
    c.setFont("Helvetica", 7.6)
    c.setFillColor(DIM)
    c.drawString(M, 0.55 * inch, "Burnish · Mastering suite for Ableton Live")
    c.drawRightString(W - M, 0.55 * inch, str(doc.page))
    c.restoreState()


def cover(c, doc):
    c.saveState()
    band = 3.55 * inch
    c.setFillColor(PANEL)
    c.rect(0, H - band, W, band, stroke=0, fill=1)
    c.setFillColor(BRAND)
    c.setFont("Helvetica-Bold", 54)
    c.drawString(M, H - 1.25 * inch, "BURNISH")
    c.setFillColor(CREAM)
    c.setFont("Helvetica-Bold", 17)
    c.drawString(M, H - 1.68 * inch, "A mastering suite, module by module:")
    c.drawString(M, H - 1.95 * inch, "from a mix to a release-ready master")
    # Six boxes: the kinds of module, left to right as a chain usually runs.
    labels = [("TONE", "EQ"), ("DYNAMICS", "COMP"), ("SPECTRAL", "ADAPT"), ("COLOUR", "SATURATE"), ("SPACE", "WIDTH"), ("LOUDNESS", "LIMIT")]
    bw, gap = 0.86 * inch, 0.2 * inch
    y = H - 2.95 * inch
    for i, (top, big) in enumerate(labels):
        x = M + i * (bw + gap)
        c.setFillColor(colors.HexColor("#111112"))
        c.setStrokeColor(CREAM)
        c.setLineWidth(1)
        c.rect(x, y, bw, 0.62 * inch, stroke=1, fill=1)
        c.setFillColor(BRAND)
        c.setFont("Helvetica-Bold", 6.6)
        c.drawString(x + 7, y + 0.46 * inch, top)
        c.setFont("Helvetica-Bold", 10)
        c.drawCentredString(x + bw / 2, y + 0.15 * inch, big)
        if i < len(labels) - 1:
            c.setStrokeColor(CREAM)
            ax = x + bw + 3
            c.line(ax, y + 0.31 * inch, ax + gap - 7, y + 0.31 * inch)
            p = c.beginPath()
            p.moveTo(ax + gap - 6, y + 0.31 * inch)
            p.lineTo(ax + gap - 11, y + 0.31 * inch + 3)
            p.lineTo(ax + gap - 11, y + 0.31 * inch - 3)
            p.close()
            c.setFillColor(CREAM)
            c.drawPath(p, stroke=0, fill=1)
    c.setFillColor(colors.HexColor("#9a978e"))
    c.setFont("Helvetica", 7.6)
    c.drawRightString(W - M, H - 3.3 * inch, "Eighteen modules, one chain, a loudness target")
    c.restoreState()
    footer(c, doc)


def later(c, doc):
    c.saveState()
    c.setFont("Helvetica-Bold", 8.2)
    c.setFillColor(BRAND_DARK)
    c.drawString(M, H - 0.6 * inch, "BURNISH")
    c.setFont("Helvetica", 8.2)
    c.setFillColor(INK)
    c.drawRightString(W - M, H - 0.6 * inch, "Mastering suite guide")
    c.setStrokeColor(RULE)
    c.setLineWidth(0.6)
    c.line(M, H - 0.68 * inch, W - M, H - 0.68 * inch)
    c.restoreState()
    footer(c, doc)


doc = BaseDocTemplate(OUT, pagesize=letter, leftMargin=M, rightMargin=M, topMargin=0.9 * inch, bottomMargin=0.85 * inch,
                      title="Burnish guide", author="Taddei", subject="Burnish: a mastering suite for Ableton Live")
doc.addPageTemplates([
    PageTemplate(id="cover", frames=[Frame(M, 0.85 * inch, FW, H - 3.55 * inch - 1.15 * inch, id="c", leftPadding=0, rightPadding=0)], onPage=cover),
    PageTemplate(id="later", frames=[Frame(M, 0.85 * inch, FW, H - 0.95 * inch - 0.85 * inch, id="l", leftPadding=0, rightPadding=0)], onPage=later)])

s = [NextPageTemplate("later")]

# ---------------------------------------------------------------------------
# Cover page
# ---------------------------------------------------------------------------
s.append(P("Burnish is a mastering suite in one plugin. You build a <b>signal chain</b> out of modules, in whatever order you like: equalisers, "
           "compressors and limiters, three adaptive spectral tools, saturation, stereo width, and a <b>Maximizer</b> that sets the final loudness. "
           "Around the chain sit loudness meters, a gain-matched bypass, dither, presets, undo and snapshots, and <b>ASSIST</b>, which listens to "
           "the song and builds a chain toward a target for you.", lead))
s.append(P("Where Lacquer gives you one knob and decides the rest, Burnish gives you every decision. That is more to learn, so this guide "
           "starts with the panel and a first session, then takes the modules one at a time, then dither and delivery. If you are new to "
           "mastering, read the first half of the Lacquer guide first: it covers what mastering is for, what LUFS and true peak mean, and why "
           "where a song is going changes how loud it should be. This guide assumes that and gets on with the tools."))
s.append(P("You'll need", h3))
s.append(bullets(["Ableton Live 11 or 12 (any edition that loads VST3 plug-ins).",
                  "Burnish built and installed. Building copies <b>Burnish.vst3</b> into ~/Library/Audio/Plug-Ins/VST3; a release download goes in the same folder.",
                  "A finished mix, and headphones or speakers you know well."]))
s.append(Spacer(1, 6))
s.append(Note("<b>The honest bit.</b> More tools make it easier to do too much. Every module here can make a mix worse as easily as better, and the "
              "usual way it goes wrong is that the master gets louder and louder sounds better. Keep <b>GAIN MATCH</b> on and press <b>BYPASS</b> "
              "often. If a change only sounds better louder, it isn't better."))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# Find your way around
# ---------------------------------------------------------------------------
s.append(P("Find your way around", h1))
marks = [(700, 72), (1062, 72), (1230, 72), (110, 92), (300, 202), (40, 262), (70, 700), (1132, 112), (1132, 530), (1132, 668), (1132, 722)]
s.append(Annotated(os.path.join(SHOTS, "overview.png"), FW * 0.84, marks, (1400, 900)))
s.append(Spacer(1, 8))
s.append(table([
    ["1", "Presets", "Whole chains, factory and yours: which modules, in what order, with their settings. Click for the list, &lt; &gt; to step. A * means changed since loading. SAVE keeps your own."],
    ["2", "Undo, snapshots", "UNDO (or cmd-Z) steps back. A B C D hold four complete states: shift-click to store, click to recall. Good for comparing two directions."],
    ["3", "Assist", "The assistant: listens, then builds a chain toward a target. See page 5."],
    ["4", "Signal chain", "One tile per module, left to right in signal order. Click to show it, drag to reorder. LED: power. S: solo. x: remove. +: add."],
    ["5", "Module header", "DELTA (hear only what this module changes), the channel mode, the module's own presets, RESET."],
    ["6", "Module display", "The spectrum of the output with the module's curves and handles on it: EQ nodes, crossovers, action regions."],
    ["7", "Module controls", "Everything else for the selected module. A line at the very bottom says in words what it is doing now."],
    ["8", "Meters, I/O gain", "Input and output, peak, RMS or loudness, L/R or M/S. A readout turns red on a clip; click it to clear."],
    ["9", "Listening", "BYPASS, GAIN MATCH (output turned to the input's loudness, so bypass is a fair test), MONO, SWAP."],
    ["10", "Dither", "Power switch and the button that opens the dither panel. Only for 16-bit (or shorter) files."],
    ["11", "Loudness", "Integrated, short-term, momentary and true peak, to ITU-R BS.1770 / EBU R128. Click to restart. The total latency is under it."]],
    [0.3 * inch, 1.25 * inch, FW - 1.55 * inch], header=False))
s.append(P("Hover over anything for a tip; TIPS turns them on and off for every Burnish. Double-click a knob, fader or readout to reset it. "
           "LCD readouts scrub when you drag them up and down. The scroll wheel works on everything.", small))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# A first session
# ---------------------------------------------------------------------------
s.append(P("A first session", h1))
s.append(table([
    ["1", "Prepare", "Export your mix as a 24-bit WAV at the session's sample rate, peaking around -6 to -3 dB, with nothing on Live's master. Make a new set, put the WAV on an audio track, and Burnish on the Master."],
    ["2", "References", "Two or three released songs in the same style on other tracks, turned down to match by ear. Solo between them and your track all the way through."],
    ["3", "Start somewhere", "Press ASSIST and let it build a chain (next page), or load a factory preset close to what you want, or start from the empty chain and add modules with +."],
    ["4", "Listen fairly", "GAIN MATCH on. BYPASS every few minutes. Each module's DELTA plays only what it adds: if you can't hear anything in DELTA, the module isn't doing anything worth its latency."],
    ["5", "Fix, then shape", "Problems first (a boomy note, harshness, rumble), then tone and dynamics, then colour and width, loudness last. See ‘Building a chain by hand’."],
    ["6", "Set the loudness", "Maximizer last in the chain. Push GAIN until the loudness block reads what the destination wants, and watch true peak stay under the ceiling."],
    ["7", "Check elsewhere", "MONO. Then a phone, earbuds, a car. Mastering is about all of them, not just your room."],
    ["8", "Rest and revisit", "Come back tomorrow with fresh ears before you send it anywhere."],
    ["9", "Export", "See ‘Dither and delivery’."]],
    [0.3 * inch, 1.25 * inch, FW - 1.55 * inch], header=False))

s.append(P("Building a chain by hand", h2))
s.append(P("Order matters: each module hears what the ones before it did. A common order, and the modules that usually live in each stage:"))
s.append(table([
    ["Stage", "Modules", "Why here"],
    ["Clean up", "Equalizer 1 (high-pass, narrow cuts), Dynamic EQ, Spectral Shaper, Low End Focus", "Problems are easiest to fix before anything else reacts to them. A compressor that is fed a boomy note pumps on it."],
    ["Tone and glue", "Dynamics or Vintage Compressor, Equalizer 2 or Vintage EQ, Stabilizer, Clarity", "Broad tone and gentle compression on a clean signal."],
    ["Colour", "Exciter, Vintage Tape", "Saturation reacts to level and tone, so it comes after they are set."],
    ["Space and punch", "Imager, Impact, Bass Control", "Width once the tone is right; microdynamics and low-end control just before the limiter."],
    ["Loudness", "Maximizer, or Vintage Limiter", "Always last (before dither), so nothing after it can push a peak over the ceiling."]],
    [1.0 * inch, 2.35 * inch, FW - 3.35 * inch]))
s.append(P("Fewer is better. Three or four modules doing small things beat ten modules each doing a little; and every spectral module adds 43 ms "
           "of latency, which Live compensates for but which makes the controls feel slow.", small))

s.append(P("Factory presets", h2))
presets = [
    ("Default", "An empty chain. Add modules with +, or run ASSIST."),
    ("Clean Master", "Gentle EQ, light glue, true-peak limiting to -1 dBTP: a safe pass for most mixes."),
    ("Loud and Clear", "Clarity and a multiband limiter for a bright, loud master."),
    ("Warm Tape", "Tape saturation, a program limiter and a touch of vintage EQ for an analogue feel."),
    ("Hip-Hop Low End", "Bass Control keeps the low end tight and loud; Impact adds punch; Maximizer mode V pushes it."),
    ("Electronic Club", "Wide tops, mono bass, dense dynamics and a loud, transient-preserving limit."),
    ("Rock Glue", "Vintage compression for glue, an exciter for bite, a limiter with some character."),
    ("Acoustic and Vocal", "Light touch: a dynamic EQ to tame harshness, Stabilizer to balance, gentle limiting."),
    ("Streaming Ready", "Aimed at -14 LUFS with a -1 dBTP ceiling, as streaming services expect."),
    ("De-harsh", "Spectral Shaper and Low End Focus to clean a mix before anything else."),
    ("Vinyl Pre-Master", "Mono bass, no clipping, moderate level: something a cutting engineer can work with."),
    ("Reference Match", "Match EQ against a captured reference, then a clean limit.")]
s.append(table([["Preset", "What it is for"]] + [[a, b] for a, b in presets], [1.35 * inch, FW - 1.35 * inch]))
s.append(P("Presets set the level of every module, but they can't know your song: the Maximizer's GAIN in particular will need moving. Several "
           "modules also have presets of their own, under PRESETS in the module header.", small))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# ASSIST
# ---------------------------------------------------------------------------
s.append(P("ASSIST", h1))
s.append(P("The assistant listens to part of the song and compares what it heard with a target: the <b>tonal balance</b> (how the energy is spread "
           "across the spectrum), the <b>loudness</b>, the <b>stereo width</b> in four bands and the <b>microdynamics</b> (how much the level moves "
           "on a short time scale) in four bands. It then builds a chain to close the gap: Equalizer 1 for tonal balance, Bass Control for the low "
           "end, Clarity and Stabilizer for detail and consistency, Impact toward the target's microdynamics, Imager toward its width, and the "
           "Maximizer for loudness. Everything it builds is ordinary modules: MODULE VIEW shows them, and they are yours to change."))
s.append(P("Setting up a run", h2))
setup = crop("assist.png", (10, 84, 1118, 890), "assist.jpg")
s.append(image(setup, FW * 0.72))
s.append(P("The setup page: target, processing, loudness.", small))
s.append(table([
    ["Target", "Eleven genre profiles, each the typical spectrum, width and dynamics of released music in that style (the LUFS beside each is that style's usual loudness, for reference). + REFERENCE makes a target from an audio file of your own: the assistant finds its loudest eight seconds and learns from those. Click the dot at the left of a target to make it a favourite; the buttons above filter the list."],
    ["Processing", "Which modules it may use. INTENSITY sets how far it moves things, from SUBTLE (keeps the mix's character) to TRANSFORMATIVE (reshapes tone and dynamics). ANALYSIS is how long it listens, 8 to 60 seconds, or bars of the host tempo with TEMPO SYNC."],
    ["Loudness", "The loudness to aim for. FULL SCALE sets a -0.1 dB ceiling with true peak off, for a CD or a file nothing will re-encode; STREAMING sets -1 dB with true peak on."],
    ["Start", "START ASSISTANT, then play the loudest, fullest part of the song, such as the last chorus, until it finishes listening. SAVE AS DEFAULT remembers the setup; ONE-CLICK starts with the saved setup without this page."]],
    [1.0 * inch, FW - 1.0 * inch], header=False))
s.append(PageBreak())
s.append(P("The result page", h2))
ar = crop("assistrun.png", (10, 84, 1118, 890), "assistrun.jpg")
s.append(image(ar, FW))
s.append(P("Target library on the left, your song's tonal balance (white) against the target (blue), loudness, and the extras.", small))
s.append(P("<b>EQUALIZER</b> scales every band it set, 0 to 200%. <b>MAXIMIZER</b> trims its gain by up to 4 dB either way, and FULL SCALE / "
           "STREAMING switch the ceiling. <b>DYNAMICS MATCH</b> and <b>WIDTH MATCH</b> scale the Impact and Imager bands; <b>CLARITY</b> and "
           "<b>STABILIZER</b> set those amounts. Each has an ON switch for a quick before-and-after. GAIN MATCH and BYPASS are here too. Clicking "
           "another target rebuilds the chain from the same listening pass. RELEARN listens again."))
s.append(Note("<b>Two things to know.</b> After it builds the chain, the Maximizer listens for five more seconds and trims its own gain toward the "
              "loudness you asked for, so keep the song playing. And picking a different target rebuilds the modules it uses, which replaces any "
              "changes you made to them by hand: choose the target first, then fine-tune. Vocal balance isn't measured, because separating a vocal "
              "from the music needs a trained model, which Burnish doesn't include."))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# The modules
# ---------------------------------------------------------------------------
s.append(P("The modules", h1))
s.append(P("In the order the + menu lists them. Every control also has a hover tip on the panel. Most modules have a <b>channel mode</b> beside "
           "DELTA: STEREO uses one set of controls for both sides; MID/SIDE and LEFT/RIGHT give each half its own set, A and B, with buttons to "
           "switch between them. The multiband modules (Dynamics, Exciter, Imager, Impact) share a <b>crossover display</b>: drag a handle to move "
           "a crossover or double-click it to type one, hover and click + to add a band, x to remove one; each band has ON and S (solo) at its top; "
           "LEARN puts the crossovers at dips in the spectrum of what is playing."))

module_shots = {}


def module(mid, name, intro, rows, extra=None):
    shot = crop("mod%d.png" % mid, (10, 180, 1118, 890), "mod%d.jpg" % mid)
    img_w = 2.6 * inch
    head = Table([[image(shot, img_w), P(intro)]], colWidths=[img_w + 0.15 * inch, FW - img_w - 0.15 * inch])
    head.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                              ("RIGHTPADDING", (0, 0), (0, 0), 10), ("TOPPADDING", (0, 0), (-1, -1), 0)]))
    block = [P(name, h2), head, Spacer(1, 6), table([["Control", "What it does"]] + rows, [1.45 * inch, FW - 1.45 * inch])]
    if extra:
        block.append(P(extra, small))
    s.append(KeepTogether(block[:3]))
    s.extend(block[3:])
    s.append(Spacer(1, 4))


module(0, "Bass Control",
       "Everything below a CUTOFF (40 to 400 Hz) is the bass it works on; BASS SOLO plays just that. The oscilloscope shows the low band's waveform "
       "with the two thresholds across it (ZOOM magnifies it); drag the lines to set them. Two sweet-spot meters show the bass against the rest of "
       "the mix and how dynamic it is: aim for green on loud, full passages.",
       [["BALANCE", "Level of the low band. The meter above: left lacks bass, right has too much."],
        ["PUNCH", "Gain on the transients of the low band only: more kick without changing its envelope the way a compressor would. The meter shows the low band's crest factor: left is squashed, right too dynamic."],
        ["SUSTAIN POWER", "Upward compression below its THRESHOLD (the dashed line): sustained bass is raised toward it, so the low end sounds fuller and steadier. Put the threshold between the kick's peaks and the sustained bass. CHARACTER blends DIRTY (adds harmonics small speakers can reproduce) and CLEAN. AMOUNT sets how much."],
        ["PEAK CONTROL", "A limiter on the low band at its THRESHOLD (the solid line): put it at the kick's peak so only the loudest hits are held. CHARACTER blends DIRTY (clipping) and CLEAN (look-ahead limiting). Keeps bass peaks from pumping the Maximizer later."]])

module(1, "Clarity",
       "An adaptive equaliser with sixty bands that contours the spectrum toward noise with a chosen tilt: resonances come down, holes come up, "
       "within 6 dB. It opens up a dull mix without the harshness of a static boost, and can make a master sound louder at the same LUFS. The "
       "curve shows the correction now; the handles set where it acts (300 Hz up). 43 ms of latency.",
       [["AMOUNT", "How far toward the target. At 100 the most it can move anything is 6 dB."],
        ["TILT", "The target: 0 dB/oct is pink noise; +3 is white (brighter); -3 is brown (darker)."],
        ["ATTACK, RELEASE", "How fast the contour follows a rise, and how fast it lets go."]])

module(2, "Dynamic EQ",
       "Six bands whose gain moves with the level in their own region. A DOWN band with a negative gain is flat until its region gets loud, then "
       "cuts: a de-esser, a tamer for one boomy note, a control for harshness that comes and goes. The white curve is static; the amber one moves "
       "with the music. Nodes work as in the Equalizer.",
       [["SHAPE, FREQ, GAIN, Q", "Baxandall bass or treble shelf, band shelf, bell or proportional-Q bell. GAIN is where the band moves to (or from) when triggered."],
        ["THRESHOLD", "The level in the band's region that triggers it. The meter beside it shows the region's level outside and the gain change inside."],
        ["DIRECTION", "UP or DOWN: which way the filter moves when triggered. The line at the bottom of the panel spells out what the current combination does."],
        ["ATTACK, RELEASE, AUTO SCALE", "How fast it moves and recovers. AUTO SCALE makes low bands slower and high bands faster."],
        ["OFFSET", "A static gain for the band, whatever the level."]])

module(3, "Dynamics",
       "Up to four bands of compression and limiting on the crossover display; each band's line shows its gain change. Each band has a compressor "
       "and a limiter on one level detector. The limiter here is the analogue kind, softer than a brickwall: the Maximizer sets the ceiling.",
       [["DETECT", "PEAK follows peaks; ENV an averaged level that treats every frequency alike; RMS the average power."],
        ["COMP, LIMIT", "The two threshold faders. Above COMP the compressor ratio applies; above LIMIT the limiter's. The meter between them shows the band's input level outside and its gain change inside."],
        ["Ratios", "Positive ratios compress downward. A negative compressor ratio, shown as (4):1, compresses <i>upward</i>: quiet parts rise toward the threshold. A negative limiter ratio expands upward, for punch."],
        ["ATTACK, RELEASE, KNEE", "Speed in and out, and how gradually it starts. ADAPTIVE REL shortens the release on transients and lengthens it on sustained sound."],
        ["PARALLEL, gains", "Dry signal mixed back per band; BAND GAIN per band; GLOBAL after all bands. AUTO GAIN matches each band's output level to its input."],
        ["Views", "CROSSOVER; GAIN TRACE of the selected band; DETECTION, a filter in front of the detector (a HIGHPASS so bass doesn't drive it, or a TILT toward the highs), with S to hear it. ALL BANDS shows every band's key controls; LINK BANDS moves them together."]])

module(4, "Equalizer 1 and 2",
       "Eight bands in seventeen shapes, drawn over the spectrum. Drag a node for frequency and gain (shift locks one axis); drag the handles "
       "beside a selected node for Q; double-click to add a band or reset a node; alt-click a node to hear only its region; delete removes it; "
       "arrow keys nudge (shift coarse, cmd fine). There are two, so one can correct and the other shape.",
       [["Shapes", "BELL; PROPORTIONAL Q (narrower the more it boosts or cuts); BAND SHELF (flat top). Shelves: plain, BAXANDALL (gentle first-order), VINTAGE (with a complementary dip), RESONANT (a bump at the corner). Passes: FLAT 6 to 48 dB/oct, RESONANT, BRICKWALL."],
        ["ANALOG, DIGITAL", "ANALOG: minimum phase, no latency. DIGITAL: linear phase, the same curve with no phase shift, 43 ms of latency."],
        ["AMOUNT", "Scales every band, 0 to 200%. The grey curve is the unscaled one."],
        ["A, B, LINK", "In MID/SIDE or LEFT/RIGHT each half has its own eight bands; ON and S per half. LINK copies a move on one half to the other."],
        ["HUD, ALL BANDS", "Edit the selected band, or every band in a table."]])

module(6, "Exciter",
       "Up to four bands of saturation, each with its own flavour, on the crossover display. POST FILTER shows where harmonics are being added "
       "and puts a high shelf on the added harmonics to tame them. The LED by each band shows how much it is adding.",
       [["MODE", "ANALOG: driven odd harmonics. RETRO: a few, decaying slowly. TAPE: brighter odd harmonics. TUBE: harder on attacks. WARM: even harmonics only. TRIODE: subtle tube. DUAL TRIODE: more drive, warmer."],
        ["AMOUNT, MIX", "How hard the band is driven; how much of the result is blended back."],
        ["Oversampling", "OFF, 2X or 4X, to keep aliasing down. Each step costs a fraction of a millisecond of latency."]])

module(7, "Imager",
       "Stereo width per band on the crossover display, and a vectorscope of the output. Bass in mono below 100 to 150 Hz is the usual "
       "mastering move: it makes the low end solid on every system.",
       [["WIDTH", "Side level per band: +100 doubles the sides, -100 makes the band mono. AMOUNT scales them all."],
        ["STEREOIZE", "For mono or narrow sources: a decorrelated copy of the middle goes into the sides so there is something to widen. Mono compatible. MODE I is a delayed copy; MODE II keeps transients sharper. Offered only while a band is being widened."],
        ["RECOVER SIDES", "When a band is narrowed, what was taken from the sides comes back in the middle instead of being lost. S plays only that. Offered only while a band is being narrowed."],
        ["Views", "CROSSOVER; CORRELATION over time (red: out of phase); WIDTH per band. Vectorscope: POLAR SAMPLE, POLAR LEVEL or LISSAJOUS, with correlation (+1 the same on both sides, -1 out of phase) and balance."]])

module(8, "Impact",
       "Microdynamics per band: how far the level moves between a hit and what follows it. One control per band expands or compresses; the "
       "traces under the display show the gain in each band over time, up for boost and down for cut.",
       [["IMPACT", "Positive opens the microdynamics up (punchier, more open); negative packs them (denser, glued). AMOUNT scales every band."],
        ["ENVELOPE", "How long an event lasts before the gain returns: milliseconds, or beat divisions of the host tempo with SYNC."],
        ["AUTO GAIN, STEREO LINK", "Keep the average level where it was; judge both sides from the middle and move them together."]])

module(9, "Low End Focus",
       "Contrast between loud and quiet moments in a low region, 20 to 300 Hz (drag the handles). Positive sharpens kick and bass transients and "
       "clears the mud under them; negative blurs the low end, the way saturation or a slow compressor would.",
       [["PUNCHY, SMOOTH", "Fast, for transients; or slow, for what sustains."],
        ["CONTRAST", "Positive or negative, as above."],
        ["GAIN, SOLO REGION", "Make-up on the region; hear the region before processing."]])

module(10, "Match EQ",
       "Captures the average spectrum of a reference and of your track and applies the difference as a smoothed, linear-phase curve. Play the "
       "reference and CAPTURE under REFERENCE, then STOP; play your track and do the same under APPLY TO. Gold is the reference, blue your track, "
       "white the match. The reference snapshot is saved with presets. 43 ms of latency.",
       [["SMOOTHING", "Higher smooths the match into a broad shape; lower follows every peak and dip, which is rarely what you want."],
        ["AMOUNT", "How much of the match to apply. Under 50% is usually wise: the aim is the reference's overall shape, not a copy."],
        ["Cutoffs", "Below the low cutoff and above the high one the match fades out, so you can match only the low end, say."]])

module(11, "Maximizer",
       "The loudness stage: GAIN into a look-ahead, true-peak limiter with an OUTPUT LEVEL ceiling, plus tools that add loudness so the limiter "
       "has less to do. Goes last.",
       [["GAIN, OUTPUT LEVEL", "Push the signal into the limiter; the ceiling the output never passes. LINK moves them in opposite directions, so you hear what the limiter does without it getting louder. -1 dB with TRUE PEAK for streaming; -0.1 for a CD."],
        ["MODE", "LOW LATENCY and I to III are single-band limiters, from the fastest to the most careful. III has STYLES: CLIPPING (loudest, roughest), CRISP, BALANCED, PUMPING. IV pre-limits three bands (CLASSIC, MODERN, TRANSIENT styles); V pre-limits four, each with its own release: the cleanest at high gain."],
        ["CHARACTER", "Release speed, CLIPPING (0) to VERY SLOW (10). Fast is louder with more distortion; slow is smoother with more pumping. 4 to 6 suits most music."],
        ["UPWARD", "Upward compression: how far quiet passages may be raised. Peaks stay put, so it adds density without more limiting."],
        ["SOFT CLIP", "Rounds peaks before the limiter, 4x oversampled. LIGHT starts at -3 dBFS, MODERATE -9, HEAVY -30; AMOUNT is wet/dry."],
        ["TRANSIENT EMPH", "Sharpens transients before limiting, so drums survive it."],
        ["STEREO INDEPENDENCE", "How independently the sides are limited, for transients and sustain separately. 0 keeps the image; 100 is louder but can narrow it."],
        ["LEARN GAIN, TARGET", "Listens for five seconds and sets GAIN so the output lands near TARGET. A starting point, not a compliance check."],
        ["Views", "SPECTRUM; GAIN TRACE (limiting from the top, upward boost from the bottom); GR METER per side."]])

module(12, "Spectral Shaper",
       "Pulls down narrow peaks that stick out of the spectral envelope, inside an action region (drag the handles; LEARN puts the region around "
       "the most resonant part of what is playing). For harshness, whistles and ringing that a static EQ would have to chase. 43 ms of latency.",
       [["MODE", "How far above the envelope a peak must be before it is touched: LOW only the worst, MEDIUM, HIGH almost anything."],
        ["AMOUNT, TONE", "How much of the excess is removed; positive TONE leaves the top brighter, negative darker."],
        ["ATTACK, RELEASE", "Time to apply a reduction and to let it go. GAIN TRACE shows the deepest reduction over time."]])

module(13, "Stabilizer",
       "An adaptive equaliser that corrects toward a tonal-balance target: the assistant's genre profiles, or ASSISTANT, the shape of a reference "
       "you gave the assistant. The grey line is the target's shape; the coloured curve is the correction now. 43 ms of latency.",
       [["TARGET, MODE", "The balance to aim for. SHAPE boosts and cuts toward it, loudness neutral; CUT only cuts what sticks out above it, which tames resonances without changing the overall balance."],
        ["AMOUNT, SPEED", "Size of the corrections (at 100 the most is 9 dB); how quickly they follow. Fast is precise but can be heard working."],
        ["SMOOTHING, SENSITIVITY", "In SHAPE mode, 100 smoothing looks like three or four broad filters, 0 like many narrow ones. Sensitivity: how small a deviation gets corrected."],
        ["TAME TRANSIENTS", "React at once to transients."],
        ["LOW, MID, HIGH", "Scale the correction below 100 Hz, from 100 Hz to 5.6 kHz, and above."]])

module(14, "Vintage Compressor",
       "A feedback compressor (its detector listens to its own output) with a filter in the loop, in the manner of the classic bus compressors. "
       "Its release is for transients; under sustained compression it lets go much more slowly, so it breathes rather than pumps.",
       [["MODE", "SHARP keeps transients crisp; BALANCED judges by the signal; SMOOTH thickens and fills."],
        ["THRESHOLD, RATIO", "Where it starts, and how much it takes off. The knee depends on the mode."],
        ["ATTACK, RELEASE, GAIN", "Speed in and out; make-up. AUTO GAIN matches the output level to the input over time."],
        ["DETECTION", "NONE; a HIGHPASS so bass doesn't pump it; a HIGH SHELF boost or a WIDE BOOST so the highs drive it. S plays what the detector hears."]])

module(15, "Vintage EQ",
       "Two rows of passive, program-style equalisation with stepped frequencies. The low shelf can boost and cut at once: the boost sits below "
       "the cut, so together they give a bump then a dip, the old trick for weight without mud. The high boost has its own bandwidth, which "
       "tightens as you boost; the mid peaks widen as they go up.",
       [["Top row", "LOW FREQ (20 to 100 Hz), LOW BOOST, LOW CUT; HIGH BOOST FREQ (3 to 16 kHz), HIGH BOOST, BANDWIDTH; HIGH CUT FREQ (5, 10, 20 kHz), HIGH CUT."],
        ["Bottom row", "LOW-MID (200 Hz to 1 kHz) boost; MID (200 Hz to 7 kHz) cut; HIGH-MID (1.5 to 5 kHz) boost."]])

module(16, "Vintage Limiter",
       "A program limiter in the manner of the variable-mu classics, then a brickwall at the CEILING. Lowering THRESHOLD limits more and makes "
       "the master louder: make-up brings the threshold up to the ceiling. An alternative to the Maximizer with more character.",
       [["MODE", "ANALOG: fast, thick, tight bass. TUBE: softer, varies with the signal. MODERN: the vintage character into a modern brickwall."],
        ["THRESHOLD, CEILING, LINK", "As above. LINK moves them together. -0.3 dB ceiling when dithering to 16 bits; -1 for lossy codecs."],
        ["TRUE PEAK, CHARACTER", "Limit peaks between samples too; attack and release from FAST (0) to SLOW (10)."]])

module(17, "Vintage Tape",
       "The response and saturation of a well-kept two-track tape machine, without the hiss and wobble. The curve is the linear part: the head "
       "bump in the lows and how far the top reaches.",
       [["SPEED", "7.5, 15 or 30 ips: slower has a bigger, higher head bump and less top end."],
        ["INPUT DRIVE", "How hard the tape is hit; the level is compensated so only the colour changes."],
        ["BIAS", "Negative: the highs distort more. Positive: saturation starts earlier, squeezing the dynamics."],
        ["HARMONICS", "Even-order colour from the electronics."],
        ["LOW, HIGH EMPHASIS", "The head bump, from flattened to +10 dB; the top end, from rolled off through flat (4) to shimmer."]])

s.append(PageBreak())

# ---------------------------------------------------------------------------
# Dither and delivery
# ---------------------------------------------------------------------------
s.append(P("Dither and delivery", h1))
dither = crop("dither.png", (10, 180, 1118, 640), "dither.jpg")
s.append(image(dither, FW * 0.8))
s.append(Spacer(1, 6))
s.append(P("A 24-bit WAV needs no dither. A 16-bit file (CD, some distributors) does: dither replaces the distortion of simply dropping the "
           "lowest bits with a tiny, steady noise, and noise shaping moves that noise to where hearing is least sensitive. Dither is the very last "
           "thing Burnish does, after the output gain, so Burnish must be the last plugin on the master, and Live's own dither must be off."))
s.append(table([["Control", "What it does"],
                ["BIT DEPTH", "The word length of the file you will export: 16 for CD, 24 for almost everything else."],
                ["DITHER AMOUNT", "OFF, LOW, MEDIUM (the usual choice) or STRONG, which removes all quantisation distortion at a slightly higher noise floor."],
                ["NOISE SHAPING", "OFF to MAX: about 14 dB less audible noise at MAX. MODERATE suits most masters."],
                ["AUTO-BLANK", "Mutes the dither after 0.7 s of digital silence, so fades end in true silence."],
                ["HARMONIC SUPPRESSION", "With dither OFF only: changes the truncation so its distortion falls away from the music's overtones."],
                ["LIMIT PEAKS", "Keeps spikes of shaped noise from going over full scale."],
                ["DC FILTER", "A 1 Hz high-pass just before the first Maximizer or Vintage Limiter in the chain, to remove DC offset. The readout shows how much there is."],
                ["BIT METER", "Which bits of the output are changing; the top row holds. With dither on, only bits within the chosen depth should light; red ones are beyond it."]],
               [1.45 * inch, FW - 1.45 * inch]))
s.append(P("Where the song is going", h2))
s.append(table([["Destination", "Loudness", "Ceiling", "Notes"],
                ["Streaming", "about -14 LUFS", "-1.0 dBTP, TRUE PEAK on", "Spotify, Apple Music, YouTube and Tidal turn louder songs down anyway; going louder gains nothing and costs punch."],
                ["Download, Bandcamp", "-11 to -9 LUFS", "-1.0 dBTP", "Played at the level you give it, so it can sit louder. Still MP3-safe at -1."],
                ["CD", "-10 to -8 LUFS", "-0.3 dBTP", "Nothing re-encodes it, so the ceiling can be nearer 0. 16-bit, with dither."],
                ["Vinyl pre-master", "-14 to -12 LUFS", "-1.0 dBTP", "Bass in mono below 150 Hz (Imager), no clipping; the cutting engineer sets the final level."]],
               [1.2 * inch, 1.05 * inch, 1.35 * inch, FW - 3.6 * inch]))
s.append(P("Exporting from Live", h2))
s.append(bullets(["Select the whole song in the arrangement, from just before the first sound to after the last tail.",
                  "File → Export Audio/Video. Rendered Track: Master. File type WAV (or AIFF); sample rate the session's (44.1 kHz for a CD).",
                  "Bit depth 24 for streaming and downloads, with Burnish's dither off. For a 16-bit CD master: 16 in Live, Burnish's DITHER on at 16, and Live's Dither Options set to No Dither.",
                  "Normalize off: it undoes the ceiling you set.",
                  "Listen to the exported file, start to end, before it goes anywhere."]))
s.append(P("An album or an EP", h2))
s.append(P("Master each song with the same chain and settings as a starting point, so they sound like one record, then adjust per song. Loudness "
           "targets measure averages, so a quiet ballad mastered to the same LUFS as a rock song will feel louder than it should: let quieter "
           "songs sit 1 to 3 LU lower. Save the chain as a preset (SAVE) so every song starts from it, and use the A B C D snapshots to flick "
           "between two songs' settings."))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# Common mistakes and reference
# ---------------------------------------------------------------------------
s.append(P("Common mistakes", h1))
s.append(bullets([
    "<b>Judging by loudness.</b> Whatever is louder wins a quick comparison. GAIN MATCH on, always, when you compare.",
    "<b>Too many modules.</b> Each one should be doing something you can hear in its DELTA. If you can't, take it out.",
    "<b>Fixing the mix in the master.</b> A buried vocal or a harsh guitar is easier, and better, fixed in the mix. Go back to the mix if you can.",
    "<b>The limiter not last.</b> Anything after the Maximizer can push a peak over the ceiling. Only dither comes after it.",
    "<b>Widening the bass.</b> Width belongs above 150 Hz or so; below it, mono is solid on every system and cuttable on vinyl.",
    "<b>Stacking spectral modules.</b> Clarity, Stabilizer, Spectral Shaper and Match EQ all react to the same spectrum. Two of them at most, and gently.",
    "<b>Normalising on export, or dithering twice.</b> Normalize off; Live's dither off when Burnish's is on.",
    "<b>Mastering on one pair of headphones.</b> Check on at least two other systems before you call it done."]))
s.append(P("Latency", h2))
s.append(table([["Module", "Latency at 48 kHz"],
                ["Clarity, Stabilizer, Spectral Shaper, Match EQ, Equalizer in DIGITAL", "43 ms each"],
                ["Maximizer", "about 1 to 5 ms, by MODE (LOW LATENCY least, V most)"],
                ["Bass Control, Vintage Limiter", "about 2 ms"],
                ["Exciter (2X, 4X), Vintage Tape", "under 1 ms"],
                ["Everything else", "none"]],
               [3.6 * inch, FW - 3.6 * inch], bold_first=False))
s.append(P("Burnish tells Live its latency, and Live lines everything up on playback; the I/O panel shows the total. Latency only matters if "
           "you play live through Burnish: then use analog EQ, Dynamics and the Maximizer in LOW LATENCY mode.", small))
s.append(P("Where things are kept", h2))
s.append(P("Your presets, module presets and assistant targets are kept in ~/Library/Application Support/Burnish (the Presets, Module Presets "
           "and Targets folders), with the settings file beside them. OPTIONS → Show settings folder opens it. Copy the folder to move "
           "everything to another computer."))
s.append(P("Keyboard and mouse", h2))
s.append(bullets(["cmd-Z: undo. Shift-click A B C D: store a snapshot; click: recall it.",
                  "EQ node: drag for frequency and gain, shift-drag to lock an axis, drag the side handles for Q, alt-click to solo its region, double-click to reset, delete to remove; arrow keys nudge (shift coarse, cmd fine).",
                  "Crossover or action-region handle: drag; double-click a crossover to type a frequency.",
                  "Any knob, fader or readout: double-click to reset; scroll to adjust."]))

doc.build(s)
print("wrote", OUT)
