# Core Concepts of a CJK Input Method Engine

## Purpose and scope

This document defines the major concepts of a library-style CJK input method engine. It describes the conceptual interface between an **engine**, which implements input and conversion logic, and a **client**, which owns the editable text and presents any user interface.

The model is intentionally narrower than desktop input method frameworks such as IBus and Fcitx:

* The engine produces data and requests operations; it does not own a panel or other user-interface component
* The client decides how composition information is displayed
* All text exchanged through the core interface is plain Unicode text
* Candidate pagination is a client-side concern, and the client decides which keys navigate a candidate list; where the highlight lands is told to the engine, because the engine may preview it
* Text selections, display carets, text-field rectangles, and other presentation details are outside the model
* Rich text and attributed text are not supported
* Engine options and configuration are exchanged through negotiation rather than represented as a secondary user interface

## Conceptual flow

The client sends the engine:

* input-context lifecycle events
* negotiation information
* key events
* surrounding text
* candidate cursor movements
* candidate selections

The engine returns:

* whether each key event was handled
* composition data
* commit text requests
* delete surrounding text requests

Each input context has independent composition state.

All engine output produced while handling a client call is delivered before
that call completes, on the calling thread. The interface is synchronous
throughout.

Independent composition state does not imply independent concurrency. Input
methods are conventionally built on libraries with process-global conversion
state — shared dictionaries, caches, and scratch working memory — so two calls
that overlap in time are unsafe even when they concern different input contexts
using different engines. This model therefore requires that calls never overlap.
It does not require them to come from one particular thread; only that they are
serialized. A single dedicated input thread is the ordinary way to satisfy this.

# Participants

## Client

The **client** is the application, operating-system component, or text-input implementation that receives the final text.

The client owns:

* the editable text
* the current insertion position
* keyboard event delivery
* presentation of composition data
* candidate pagination, and the key bindings that navigate a candidate list
* application of text-editing requests

The client may represent an entire application window, an individual text field, or another independently editable destination.

## Engine

The **engine** implements a particular input method or family of input methods.

Examples include engines for:

* Pinyin
* Zhuyin
* Japanese kana and kanji conversion
* Korean Hangul composition
* shape-based Chinese input
* dictionary-backed prediction

The engine receives key events and contextual information, maintains composition state for each input context, and produces composition data or editing requests.

An engine does not directly modify the client's text. It asks the client to perform operations such as committing or deleting text.

## Input context

An **input context** represents one independently editable client destination and the engine state associated with it.

An input context normally has its own:

* composition state
* composition data
* surrounding text
* focus state
* negotiated capabilities and options

Creating an input context begins the lifetime of this per-destination state. Destroying the input context ends that lifetime.

The same engine may serve many input contexts simultaneously. State that depends on partially entered text must therefore belong to an input context rather than to the engine globally.

**IBus and Fcitx note:** Both frameworks use the term *input context*. IBus describes its input context as the object through which a client invokes an engine. Fcitx describes an input context as representing a client, which may be a window or an individual text field. ([Intelligent Input Bus][1])

# Key input

## Key event

A **key event** represents a key press sent by the client to the engine.

A key event contains:

* the logical key — the character or named key the client's layout produced
* the physical key — which key position was pressed, independent of layout
* modifier state

These three are independent descriptions of the same press, not inputs to a
calculation the engine performs. The logical key already has every
transformation the client's layout applies: pressing Shift and Q reports the
logical key `Q`, together with a Shift modifier, and the engine may neither
derive `Q` from `q` because Shift was reported nor infer Shift because the
logical key is uppercase. The physical key is likewise reported unmodified, so
that an engine defined by key position can combine position and modifiers on its
own terms.

Modifier state exists mainly so that engines can decline what is not theirs. A
key chorded with a control, alt, or command modifier is a client command, and an
engine reports it unhandled rather than absorbing it. Where a modifier does
affect composition it is because the engine recombines it with the physical key,
as Hangul does to reach a doubled jamo.

The set of logical keys is open-ended, and an engine is not expected to
recognize all of them. An unrecognized key is not an error; it is simply
unhandled.

The physical key is optional. Clients with no physical keyboard, such as
on-screen and predictive keyboards, omit it, and every engine must work without
it. It exists because some input methods are defined by key position rather
than by character: Hangul composition assigns jamo to positions, so a client
using a non-US layout must report position for those assignments to be correct.

Key releases are not part of this model. Engines see key presses only. This
excludes input methods that depend on release timing, such as thumb-shift
(Nicola) kana layouts.

Conceptually, the engine is given an opportunity to process the event and must report whether it was handled.

## Handled

A key event is **handled** when the engine accepts responsibility for it.

When a key event is handled, the client must not also process the original event through its ordinary text-input path. Processing it twice could insert duplicate text or perform an unintended client command.

A key event is **unhandled** when the engine declines responsibility for it. The client may then process the original event normally.

Handled status applies to the original incoming event. It is separate from any commit text or composition data produced while processing that event. An engine may absorb a key into its composition state, emit the resulting text, and still report the event unhandled.

Because engine output is delivered before the key-processing call completes, an unhandled event is already correctly ordered against any commit text or composition change the same event produced. The client applies the engine's output first and then processes the key by its normal path.

**IBus and Fcitx note:** IBus key processing returns a Boolean result indicating whether the engine successfully processed the key. Fcitx expresses the equivalent behavior by accepting its key-event object. This document calls that result *handled*. ([Intelligent Input Bus][2])

Both frameworks additionally provide a *forward key event* operation, by which the engine asks the client to receive a key event as engine output rather than declining the original. Its purpose is to order a declined key against other output when the handled result is reported asynchronously. This model has no such operation: the interface is synchronous, so ordering is already guaranteed, and forwarding is unevenly implemented across client toolkits. Input methods that depend on delivering delayed, transformed, or engine-generated keys are therefore outside this model.

# Composition

## Composition data

**Composition data** is the current composition-related data produced by the engine for an input context.

Composition data contains exactly two conceptual fields:

1. preedit text
2. a candidate list, together with the *candidate cursor* into it

Composition data is plain data, not a user-interface component. The client chooses whether and how to display it.

A client may display the fields:

* inline with the application's text
* in a floating window
* in a fixed area
* in an on-screen keyboard
* using an accessibility interface
* in any other client-appropriate form

The current composition data supersedes the previous composition data for that input context. Empty text or an empty candidate list indicates that the corresponding information is not currently present.

Because the whole of it is replaced at once, a client redraws from what it was last given rather than from anything it remembers requesting. That matters most for the candidate cursor, which the engine moves as well as the client — see *Candidate cursor*.

**IBus and Fcitx note:** IBus sends preedit text, auxiliary text, and its lookup table through separate update operations. Fcitx groups related values in an input-panel object containing preedit, upper and lower auxiliary text, and a candidate list. This model instead combines these into one neutral composition-data value and does not model the panel itself. It has **no auxiliary text at all**: see *Preedit text* for where the content both frameworks put there belongs instead. ([Intelligent Input Bus][2])

## Preedit text

**Preedit text** is text that represents the user's current composition but has not yet been committed to the client.

Examples include:

* partially entered Pinyin
* kana awaiting kanji conversion
* an incomplete Hangul syllable sequence
* a converted phrase that has not yet been confirmed

Preedit text is provisional. It may be replaced, extended, shortened, or removed as the user continues entering text.

One rule fixes what it contains, for every engine:

> Preedit text is what the user has settled, followed by what they have typed and not yet settled, rendered in the script they are composing in. An engine does not rewrite it with a conversion the user has not chosen.

So a Japanese engine shows kana, a Pinyin engine shows syllables, a Bopomofo engine shows zhuyin, and a table-based engine shows its key run — each preceded by whatever the user has already chosen. An engine's guesses are the *candidate list*, which the user may take or ignore; they do not appear in the preedit unasked.

The guarantee this gives a client: preedit text is what would be committed if the composition ended now. The only departures are normalizations an engine can only apply at the moment of commit — resolving a trailing romaji consonant that one more keystroke would still change, or dropping separators rendered between syllables. Neither changes which characters the user chose.

This is also where the content other frameworks carry as *auxiliary text* belongs. What a user has typed is not supplemental to their composition; it is the composition. Auxiliary text as those frameworks use it in practice is either that same content displaced, or a candidate position indicator, which is presentation derived from the *candidate cursor* and the candidate count.

Preedit text is accompanied by an internal display position which reflects the state of the engine. Text prior to this position is not expected to be changed on commit, while text beyond this position is still subject to change based on user input. For example, if a user has typed a pinyin string and then selected a partial candidate, the preedit display position would move forward to reflect the selected characters.

Preedit text is a single plain-text string. It contains no:

* formatting attributes
* styled segments
* underline information
* internal display position
* client-side caret information

Empty preedit text means that no preedit text is currently present.

**IBus and Fcitx note:** IBus models a pre-edit buffer with a visibility flag and a position within the buffer. Fcitx distinguishes panel preedit from client preedit and supports formatted text. This model has only one plain preedit string; the client decides where it is shown. ([Intelligent Input Bus][2])

## Candidate list

A **candidate list** is an ordered list of plain-text alternatives supplied by the engine.

Candidates may represent:

* possible characters
* possible words
* possible phrases
* alternative conversions
* predictions
* completions

The candidate list is the complete logical list available for selection. It is not divided into engine-defined pages.

Every candidate in a list is an alternative for the same span of input: the leftmost portion of the composition that is not yet settled. There is never more than one span under consideration at a time, and the client does not choose which span that is. This is what makes selection greedy — see *Select candidate*.

An engine whose underlying source enumerates alternatives lazily, or without a knowable total, bounds the list at a negotiated maximum and presents the bounded result as the complete list. The client is never required to request more candidates in order to display or paginate what it has. Raising the bound only appends; it never reorders or renumbers candidates already supplied.

Each candidate consists only of its plain text. The core model does not attach:

* labels
* shortcut keys
* comments
* annotations
* formatting
* actions
* identifiers
* page numbers
* layout hints
* highlighted positions

The client may paginate, scroll, group, or otherwise present the list without informing the engine. Such presentation does not alter candidate positions.

The order of a candidate list remains meaningful until the engine supplies new composition data. New composition data replaces the previous candidate list and invalidates positions referring to the previous list.

**IBus and Fcitx note:** IBus calls its candidate list a *lookup table*. An IBus lookup table also includes page size, a candidate cursor, wrapping behavior, orientation, and candidate labels. Fcitx candidate lists may similarly provide paging, cursor movement, labels, comments, placeholders, and other interfaces. Of those, only the cursor is present here — see *Candidate cursor* — and it is present because it is not merely presentation. Page size, wrapping, orientation, and labels are excluded.
IBus lookup table pagination is controlled by key press events rather than an API, which is undesirable. ([Intelligent Input Bus][3])

## Candidate cursor

The **candidate cursor** is the position in the candidate list that a client draws highlighted.

It is **part of composition data**, not a separate query and not something the client tracks privately. It is a property of the list rather than of any candidate, it is always a position within the current list, and it is at the first position whenever the list is empty.

The cursor exists in this model — where page size, labels and orientation do not — because it is not purely presentation. On some engines it **previews**: moving the cursor rewrites the unsettled span of the preedit to that candidate's text. The highlight and the preedit are then two views of one fact, and a model that let the client own the highlight privately would leave them unable to agree.

When the cursor previews follows from the rule in *Preedit text* rather than being a per-engine quirk — and it is a property of the moment, not of the engine. The preedit only ever shows a conversion the user asked for, so the cursor rewrites it exactly where the user has already asked, and only there. Candidates that appeared in response to a request for conversion are being chosen *among*, so moving the cursor through them is being told *which* conversion, and the preedit follows. Candidates the engine volunteered unasked — from the first keystroke, or however they arrived — are being *browsed*, and moving the cursor through them settles nothing and moves no text. One engine can be in each state at different moments: a Japanese engine offering candidates while the user types is being browsed, and the same list after the convert key is being chosen among. Either way the invariant holds: ending the composition commits what is on screen.

A request for conversion made while the cursor is browsing **adopts** the hovered candidate: conversion begins previewing the entry the cursor was on, not the first entry. A hover is the user's most recent expression of interest, and a convert key that discarded it would surprise exactly the client interfaces — a phone keyboard's suggestion strip, a gamepad-driven selector — where moving the highlight is deliberate work. When the cursor was never moved it sits at the first position and adoption is invisible.

Moving the cursor settles nothing. It commits no text, it does not advance the preedit display position, and it can be undone by moving the cursor back. That is what distinguishes it from *Select candidate*, which is irrevocable.

The cursor moves in three ways, and only these:

* the client moves it, by absolute position
* a key event that asks for conversion advances it, on an engine that converts by cycling through its candidates — the first such press adopts the position the cursor is already at, and each further press advances it
* new composition data replaces the list, which returns the cursor to the first position

Two of those three are not the client's, which fixes the client's obligation: **a client draws its highlight from the current composition data, and does not assume the cursor is where it last put it.** Asking for a cursor position is a request, not an assignment. Every one of the three movements arrives as new composition data, so a client that redraws from what it is given is never out of step.

The client owns the key bindings. Which key moves the cursor, in which direction, and whether either end of the list wraps are decisions the engine takes no part in — the engine is told the resulting position and nothing else. There are correspondingly no "next" and "previous" operations in this model, only movement to an absolute position.

An engine that produces no candidates has no cursor to move.

## Select candidate

**Select candidate** tells the engine that the client has chosen a candidate from the current candidate list.

The candidate is identified by its absolute position in the complete list most recently supplied by the engine.

The position is not relative to:

* a displayed page
* a visible range
* a row or column
* a client-side highlighted position

For example, if the client displays candidates 20 through 29 as its third page, selecting the fourth displayed candidate selects absolute position 23, subject to the indexing convention chosen by the API.

Navigating to a candidate and choosing one are separate operations. Navigation moves the *Candidate cursor* and settles nothing; selection is what the client sends when the user has chosen. A client is free to select a position the cursor never visited — selection takes an absolute position and does not depend on where the cursor has been.

Selection resolves the composition greedily, from the beginning toward the end. Choosing a candidate settles the portion of the composition that candidate covers, advances the preedit display position past it, and produces a fresh candidate list for whatever input remains. When nothing remains, the engine commits.

The client therefore never navigates between, or adjusts the boundaries of, the divisions an engine may use internally to segment the input. Engines whose underlying conversion is multi-segment expose only the region currently being resolved. Segment navigation and segment resizing are outside this model.

The client must not select a position from an obsolete candidate list after newer composition data has been received.

**IBus note:** IBus candidate selection is engine-specific and controlled via key press events rather than an API. This is undesirable. ([Intelligent Input Bus][2])

# Client text and editing

## Surrounding text

**Surrounding text** is plain text from the client near its current insertion position, supplied to the engine as context.

It consists conceptually of:

* a plain-text string
* the current insertion position within that string

The supplied string may be only part of the client's document. The engine must not assume that it contains the entire document or that its beginning and end are document boundaries.

Surrounding text may be used for:

* context-sensitive conversion
* punctuation decisions
* prediction
* reconversion
* detecting preceding or following characters
* deciding whether nearby text should be deleted or replaced

This model does not expose a client text selection. It therefore has no selection anchor. The insertion position is included only to divide the supplied text into text before and after the point of input; it is not an IME display-caret concept.

Availability of surrounding text is negotiated. A client that cannot or should not expose surrounding text simply does not supply it. An engine that cannot operate without it declares that as a requirement, and the pairing is rejected when the input context is created rather than failing silently later.

**IBus and Fcitx note:** Both IBus and Fcitx surrounding-text representations include a cursor position and a separate anchor position for representing selections. This model retains only one insertion position and ignores selection. ([Intelligent Input Bus][2])

## Commit text

**Commit text** is a request from the engine to insert finalized plain text into the client.

Committed text becomes ordinary client text. It is no longer provisional composition state.

Commit text may result from:

* confirming the current composition
* selecting a candidate
* automatic conversion
* direct input handled by the engine
* prediction or completion
* another engine-defined action

The client applies the insertion using its normal text-editing behavior.

Committing text does not implicitly define the next composition data. After committing, the engine supplies whatever composition data should remain, commonly empty composition data or the state of a newly started composition.

**IBus and Fcitx note:** IBus uses the term *commit text*. Fcitx calls its equivalent operation *commit string*. ([Intelligent Input Bus][2])

## Delete surrounding text

**Delete surrounding text** is a request from the engine for the client to delete part of its existing surrounding text.

The range is described by:

* an offset relative to the current insertion position
* a character count

A negative offset refers to text before the insertion position. A positive offset refers to text after it.

The frame of reference is the surrounding text the client most recently supplied, and the origin is the insertion position reported with it — not wherever the client's insertion point may have moved since. An engine may only ask to delete text it can actually see, so the requested range always lies within the supplied surrounding text. A client whose document has changed since it last reported surrounding text is therefore free to ignore the request instead of deleting something else.

Deletion is ordered ahead of any commit text produced by the same client call, so that the range is always relative to the document as the engine last saw it rather than to the result of a commit the client has just applied.

At most one deletion is requested per client call. Several would each be described against the same surrounding text, so applying the first would move the text the second describes, and every client would have to collect them and apply them in reverse order. One request, applied where it says.

This operation may be used for:

* reconversion
* replacing preceding text
* removing an automatically inserted character
* combining a new composition with existing text
* correcting context-dependent punctuation or spacing

The request is meaningful only if the client supports deletion of surrounding text and the engine has sufficiently current surrounding-text information.

The concrete API must define the Unicode unit used for offsets and counts, such as Unicode scalar values. Byte offsets must not be assumed.

**IBus and Fcitx note:** Both frameworks expose deletion using an offset relative to the current text position and a character count. IBus describes negative offsets as positions before the cursor; Fcitx describes its deletion lengths in UCS-4 characters. ([Intelligent Input Bus][1])

# Input-context lifecycle

## Focus

**Focus** indicates whether an input context currently corresponds to the client destination receiving input.

Focus is a property of the client and its input context.

The client informs the engine when the input context:

* gains focus
* loses focus

Focus gates input and nothing else. An unfocused input context does not accept key events, candidate cursor movements, or candidate selections; reading composition data — including the candidate cursor — supplying surrounding text, changing settings, and resetting all remain available.

Losing focus neither commits nor discards. Composition state is preserved unchanged, so regaining focus resumes exactly where the user left off, and the engine produces no output as a result of the transition itself. A client that wants the preedit finalized or abandoned when the user leaves a field performs that itself, before dropping focus. Making this a fixed rule rather than negotiated behavior keeps a text field's contents a decision the client owns, which matters because engines disagree: some underlying libraries flush their pending syllable on focus loss and others ignore focus entirely.

An input context begins its lifetime unfocused.

**IBus and Fcitx note:** IBus exposes separate focus-in and focus-out notifications. Fcitx input contexts also explicitly gain and lose focus. ([Intelligent Input Bus][2])

Both frameworks also carry an *activation* concept — IBus enable/disable, Fcitx activate/deactivate — indicating whether the engine is selected and enabled for a context, independently of focus. This model does not. A client that wants direct keyboard input, or that has selected another engine, stops sending key events to the input context; no separate state is required to express it.

## Reset

**Reset** asks the engine to discard its current transient composition state for an input context and return to a neutral state.

After a reset, the engine should produce empty composition data unless it has a reason to begin a new composition immediately.

Reset does not commit preedit text. An engine that needs to preserve text must issue commit text explicitly before or as part of handling the reset.

Reset does not destroy the input context. Negotiated information and other persistent per-context settings remain in effect.

Reset may be requested when:

* the client abandons an edit
* input is canceled
* the document changes in a way that invalidates engine state
* the input destination changes unexpectedly
* the client needs to resynchronize with the engine

**IBus and Fcitx note:** Both IBus and Fcitx expose reset as a distinct engine operation. Fcitx documents reset as clearing input-context state and, by default, calls it during engine deactivation. ([Intelligent Input Bus][2])

# Negotiation

## Negotiation

**Negotiation** is the exchange of capabilities, field information, options, and behavioral agreements between the client and engine.

Negotiation may take place when an input context is created and may be updated during its lifetime.

Negotiated information may include:

### Client capabilities

Which operations the client supports, such as:

* displaying composition data
* supplying surrounding text
* deleting surrounding text

A client declares these by implementing the corresponding operations, not by
describing them separately. An operation the client has not implemented is one
it does not support. There is no capability description that can disagree with
what the client actually does.

### Engine requirements

The engine may indicate that it:

* requires surrounding text
* requires the ability to delete surrounding text
* requires a particular key representation
* supports optional language or conversion features

Requirements travel from engine to client, which is the direction that carries
information: the client cannot know what a given input method needs, and an
engine may need more in some configurations than others.

Requirements are checked when an input context is created. Pairing an engine
with a client that lacks a required operation is an error at that point, rather
than a silent loss of engine output later.

### Input purpose and hints

The client may describe the kind of text being edited, such as:

* ordinary text
* a person's name
* an email address
* a URL
* a number
* a telephone number
* a password or other sensitive text
* single-line or multiline text

Hints may also request or discourage behaviors such as:

* spelling assistance
* prediction
* completion
* automatic capitalization
* on-screen keyboard use

### Engine options

Engine-specific options and configuration values may be included in negotiation.

Examples include:

* simplified versus traditional Chinese
* punctuation style
* full-width versus half-width characters
* kana input mode
* dictionary selection
* prediction behavior
* conversion preferences

These are data exchanged with the engine. This core model does not define a menu, language bar, property list, configuration window, or other secondary user interface.

### Behavioral policies

Negotiation may define policies such as:

* the maximum size of a candidate list
* how unsupported operations are reported
* protocol versioning
* extension support

**IBus and Fcitx note:** IBus represents client display and surrounding-text abilities as capability flags, while input purpose and hints are exposed separately as content type. Fcitx uses a larger capability-flag set containing both client features and field-purpose hints. Those flags exist largely to divide responsibility between the engine and a separate panel component. This model has no such division — the client presents everything — so it has no client capability flags, and describes client support by which operations the client implements. Both frameworks also have separate property or configuration systems; this model groups the information relevant to the library boundary under negotiation and excludes any prescribed configuration UI. ([Intelligent Input Bus][4])

# Plain-text rule

All textual data in the core model is plain Unicode text:

* preedit text
* candidate text
* surrounding text
* commit text

The core interface does not support:

* text attributes
* rich-text spans
* foreground or background colors
* underlines
* segment styling
* candidate comments
* candidate labels
* embedded icons
* presentation markup

An implementation may add such features through an optional extension, but they are not part of the concepts defined here.

# Explicitly excluded concepts

The following are outside this model:

* an IME-owned panel or window
* panel placement or geometry
* a client text-field rectangle
* an IME display caret
* client text selections
* client preedit versus panel preedit (fcitx terms)
* candidate highlighting
* candidate navigation
* candidate paging
* page-relative candidate positions
* candidate labels and shortcut keys
* candidate comments and actions
* candidate layout or orientation
* segment navigation and segment resizing
* rich or attributed text
* status areas and language bars
* property menus
* configuration user interfaces
* engine discovery and engine switching
* handwriting input
* virtual-keyboard presentation
* forwarded key events
* engine activation state, as distinct from focus
* key release events

These may be useful features of a complete input-method framework, but they are not required to define the engine library interface described by this document.

# Canonical vocabulary

The canonical terms used by this documentation are:

| Term                        | Meaning                                                             |
| --------------------------- | ------------------------------------------------------------------- |
| **Client**                  | The owner of the editable text and presentation.                    |
| **Engine**                  | The implementation of input and conversion logic.                   |
| **Input context**           | Per-client-destination engine state and negotiated information.     |
| **Key event**               | A key press offered to the engine.                                  |
| **Handled**                 | The engine has accepted responsibility for the original key event.  |
| **Unhandled**               | The client may process the original key event normally.             |
| **Composition data**        | Preedit text and the candidate list.                                |
| **Preedit text**            | Provisional text that has not been committed: what the user settled, then what they typed. |
| **Candidate list**          | The complete ordered list of selectable candidate texts.            |
| **Candidate cursor**        | Composition data: the highlighted list position. Moving it settles nothing. |
| **Select candidate**        | Choose a candidate by its absolute position in the current list.    |
| **Surrounding text**        | Client text near the insertion position, supplied as context.       |
| **Commit text**             | Insert finalized text into the client.                              |
| **Delete surrounding text** | Delete client text using an offset and character count.             |
| **Focus**                   | Whether the input context is currently receiving client input.      |
| **Reset**                   | Discard transient composition state without destroying the context. |
| **Negotiation**             | Exchange capabilities, field information, options, and policies.    |

[1]: https://ibus.github.io/docs/ibus-1.5/IBusInputContext.html "IBusInputContext"
[2]: https://ibus.github.io/docs/ibus-1.5/IBusEngine.html "IBusEngine"
[3]: https://ibus.github.io/docs/ibus-1.5/IBusLookupTable.html "IBusLookupTable"
[4]: https://ibus.github.io/docs/ibus-1.5/ibus-ibustypes.html "ibustypes"
