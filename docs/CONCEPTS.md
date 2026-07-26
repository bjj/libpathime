# Core Concepts of a CJK Input Method Engine

## Purpose and scope

This document defines the major concepts of a library-style CJK input method engine. It describes the conceptual interface between an **engine**, which implements input and conversion logic, and a **client**, which owns the editable text and presents any user interface.

The model is intentionally narrower than desktop input method frameworks such as IBus and Fcitx:

* The engine produces data and requests operations; it does not own a panel or other user-interface component
* The client decides how composition information is displayed
* All text exchanged through the core interface is plain Unicode text
* Candidate navigation and pagination are client-side concerns
* Text selections, display carets, text-field rectangles, and other presentation details are outside the model
* Rich text and attributed text are not supported
* Engine options and configuration are exchanged through negotiation rather than represented as a secondary user interface

## Conceptual flow

The client sends the engine:

* input-context lifecycle events
* negotiation information
* key events
* surrounding text
* candidate selections

The engine returns:

* whether each key event was handled
* composition data
* commit text requests
* delete surrounding text requests
* forward key event requests

Each input context has independent composition state.

# Participants

## Client

The **client** is the application, operating-system component, or text-input implementation that receives the final text.

The client owns:

* the editable text
* the current insertion position
* keyboard event delivery
* presentation of composition data
* candidate navigation and pagination
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
* activation state
* negotiated capabilities and options

Creating an input context begins the lifetime of this per-destination state. Destroying the input context ends that lifetime.

The same engine may serve many input contexts simultaneously. State that depends on partially entered text must therefore belong to an input context rather than to the engine globally.

**IBus and Fcitx note:** Both frameworks use the term *input context*. IBus describes its input context as the object through which a client invokes an engine. Fcitx describes an input context as representing a client, which may be a window or an individual text field. ([Intelligent Input Bus][1])

# Key input

## Key event

A **key event** represents a key press sent by the client to the engine.

A key event may contain information such as:

* the logical key
* the physical key
* modifier state

The exact representation is an API design detail. Conceptually, the engine is given an opportunity to process the event and must report whether it was handled.

## Handled

A key event is **handled** when the engine accepts responsibility for it.

When a key event is handled, the client must not also process the original event through its ordinary text-input path. Processing it twice could insert duplicate text or perform an unintended client command.

A key event is **unhandled** when the engine declines responsibility for it. The client may then process the original event normally.

Handled status applies to the original incoming event. It is separate from any commit text, composition data, or forward key event produced while processing that event.

**IBus and Fcitx note:** IBus key processing returns a Boolean result indicating whether the engine successfully processed the key. Fcitx expresses the equivalent behavior by accepting its key-event object. This document calls that result *handled*. ([Intelligent Input Bus][2])

## Forward key event

A **forward key event** is an explicit request from the engine for the client to receive a key event.

Forwarding is not the same as reporting the original event as unhandled:

* An unhandled event continues through the client's normal processing path
* A forwarded event is an output produced by the engine

A forwarded event may be:

* a delayed version of an earlier event
* a transformed event
* an event generated by the engine
* an event that must be delivered after other engine output

The client should treat the forwarded event as a new event delivered by the engine and should avoid sending it back through the same engine in a way that creates a loop.

**IBus and Fcitx note:** IBus exposes a forward key event operation from the engine to the client. Fcitx similarly allows an input context to send a key event to its client. ([Intelligent Input Bus][1])

# Composition

## Composition data

**Composition data** is the current composition-related data produced by the engine for an input context.

Composition data contains exactly three conceptual fields:

1. preedit text
2. auxiliary text
3. a candidate list

Composition data is plain data, not a user-interface component. The client chooses whether and how to display it.

A client may display the fields:

* inline with the application's text
* in a floating window
* in a fixed area
* in an on-screen keyboard
* using an accessibility interface
* in any other client-appropriate form

The current composition data supersedes the previous composition data for that input context. Empty text or an empty candidate list indicates that the corresponding information is not currently present.

**IBus and Fcitx note:** IBus sends preedit text, auxiliary text, and its lookup table through separate update operations. Fcitx groups related values in an input-panel object containing preedit, upper and lower auxiliary text, and a candidate list. This model instead combines the three concepts into one neutral composition-data value and does not model the panel itself. ([Intelligent Input Bus][2])

## Preedit text

**Preedit text** is text that represents the user's current composition but has not yet been committed to the client.

Examples include:

* partially entered Pinyin
* kana awaiting kanji conversion
* an incomplete Hangul syllable sequence
* a converted phrase that has not yet been confirmed

Preedit text is provisional. It may be replaced, extended, shortened, or removed as the user continues entering text.

Preedit text is accompanied by an internal display position which reflects the state of the engine. Text prior to this position is not expected to be changed on commit, while text beyond this position is still subject to change based on user input. For example, if a user has typed a pinyin string and then selected a partial candidate, the preedit display position would move forward to reflect the selected characters.

Preedit text is a single plain-text string. It contains no:

* formatting attributes
* styled segments
* underline information
* internal display position
* client-side caret information

Empty preedit text means that no preedit text is currently present.

**IBus and Fcitx note:** IBus models a pre-edit buffer with a visibility flag and a position within the buffer. Fcitx distinguishes panel preedit from client preedit and supports formatted text. This model has only one plain preedit string; the client decides where it is shown. ([Intelligent Input Bus][2])

## Auxiliary text

**Auxiliary text** is optional plain text that supplements the current composition.

It may contain:

* an input-mode indication
* a conversion hint
* a short explanation
* an error or warning
* additional information about the candidates
* status associated with the current composition

Auxiliary text is not committed to the client's editable text.

There is one auxiliary-text string. Its placement relative to the preedit text or candidate list is entirely a client decision.

Empty auxiliary text means that no auxiliary text is currently present.

**IBus and Fcitx note:** IBus provides one auxiliary-text value associated with an auxiliary bar. Fcitx provides separate upper and lower auxiliary-text values. This model uses one placement-independent value. ([Intelligent Input Bus][2])

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

**IBus and Fcitx note:** IBus calls its candidate list a *lookup table*. An IBus lookup table also includes page size, a candidate cursor, wrapping behavior, orientation, and candidate labels. Fcitx candidate lists may similarly provide paging, cursor movement, labels, comments, placeholders, and other interfaces. Those features are intentionally excluded here.
IBus lookup table pagination is controlled by key press events rather than an API, which is undesirable. ([Intelligent Input Bus][3])

## Select candidate

**Select candidate** tells the engine that the client has chosen a candidate from the current candidate list.

The candidate is identified by its absolute position in the complete list most recently supplied by the engine.

The position is not relative to:

* a displayed page
* a visible range
* a row or column
* a client-side highlighted position

For example, if the client displays candidates 20 through 29 as its third page, selecting the fourth displayed candidate selects absolute position 23, subject to the indexing convention chosen by the API.

Candidate navigation is performed entirely by the client. The engine receives only the completed selection.

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

Availability of surrounding text is negotiated. A client that cannot or should not expose surrounding text may report that it is unsupported.

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

Focus is a property of the client and its input context. It does not by itself determine whether a particular engine is active.

The client informs the engine when the input context:

* gains focus
* loses focus

Focus changes allow the engine to preserve, discard, commit, or otherwise manage composition state according to negotiated behavior.

No particular commit or reset behavior is implied solely by the word *focus*. Such behavior must be defined by the engine contract or negotiation.

**IBus and Fcitx note:** IBus exposes separate focus-in and focus-out notifications. Fcitx input contexts also explicitly gain and lose focus. Fcitx engine activation may occur in connection with focus, but activation and focus remain distinguishable concepts in this model. ([Intelligent Input Bus][2])

## Activation

**Activation** indicates whether the engine is selected and enabled for an input context.

An input context may have focus while the engine is inactive, for example when:

* direct keyboard input is selected
* another engine is selected
* input-method processing is disabled for that field

Activation and focus are independent:

* A focused input context may have an inactive engine
* An active engine may retain state for an input context that temporarily lacks focus

The client or hosting system informs the engine when it becomes active or inactive for an input context.

Activation or deactivation does not inherently imply a commit or reset unless the engine contract or negotiation specifies one.

**IBus and Fcitx note:** IBus calls the corresponding lifecycle operations enable and disable. Fcitx calls its engine hooks activate and deactivate. Fcitx's default deactivation implementation calls reset, but this document treats that as a policy rather than part of the definition of deactivation. ([Intelligent Input Bus][2])

## Reset

**Reset** asks the engine to discard its current transient composition state for an input context and return to a neutral state.

After a reset, the engine should produce empty composition data unless it has a reason to begin a new composition immediately.

Reset does not commit preedit text. An engine that needs to preserve text must issue commit text explicitly before or as part of handling the reset.

Reset does not destroy the input context. Negotiated information and other persistent per-context settings may remain in effect.

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

Examples include whether the client supports:

* composition data
* surrounding text
* delete surrounding text
* forwarded key events
* particular ordering guarantees
* optional protocol extensions

### Engine requirements

The engine may indicate that it:

* wants surrounding text
* may request deletion
* requires a particular key representation
* supports optional language or conversion features

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

* what happens to preedit text when focus is lost
* whether state persists while the engine is inactive
* how unsupported operations are reported
* protocol versioning
* extension support

**IBus and Fcitx note:** IBus represents client display and surrounding-text abilities as capability flags, while input purpose and hints are exposed separately as content type. Fcitx uses a larger capability-flag set containing both client features and field-purpose hints. Both frameworks also have separate property or configuration systems. This model groups the information relevant to the library boundary under negotiation and excludes any prescribed configuration UI. ([Intelligent Input Bus][4])

# Plain-text rule

All textual data in the core model is plain Unicode text:

* preedit text
* auxiliary text
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
* rich or attributed text
* status areas and language bars
* property menus
* configuration user interfaces
* engine discovery and engine switching
* handwriting input
* virtual-keyboard presentation

These may be useful features of a complete input-method framework, but they are not required to define the engine library interface described by this document.

# Canonical vocabulary

The canonical terms used by this documentation are:

| Term                        | Meaning                                                             |
| --------------------------- | ------------------------------------------------------------------- |
| **Client**                  | The owner of the editable text and presentation.                    |
| **Engine**                  | The implementation of input and conversion logic.                   |
| **Input context**           | Per-client-destination engine state and negotiated information.     |
| **Key event**               | A key press or release offered to the engine.                       |
| **Handled**                 | The engine has accepted responsibility for the original key event.  |
| **Unhandled**               | The client may process the original key event normally.             |
| **Forward key event**       | An explicit engine request to deliver a key event to the client.    |
| **Composition data**        | Preedit text, auxiliary text, and the candidate list.               |
| **Preedit text**            | Provisional text that has not been committed.                       |
| **Auxiliary text**          | Supplemental plain text associated with the composition.            |
| **Candidate list**          | The complete ordered list of selectable candidate texts.            |
| **Select candidate**        | Choose a candidate by its absolute position in the current list.    |
| **Surrounding text**        | Client text near the insertion position, supplied as context.       |
| **Commit text**             | Insert finalized text into the client.                              |
| **Delete surrounding text** | Delete client text using an offset and character count.             |
| **Focus**                   | Whether the input context is currently receiving client input.      |
| **Activation**              | Whether the engine is selected and enabled for the input context.   |
| **Reset**                   | Discard transient composition state without destroying the context. |
| **Negotiation**             | Exchange capabilities, field information, options, and policies.    |

[1]: https://ibus.github.io/docs/ibus-1.5/IBusInputContext.html "IBusInputContext"
[2]: https://ibus.github.io/docs/ibus-1.5/IBusEngine.html "IBusEngine"
[3]: https://ibus.github.io/docs/ibus-1.5/IBusLookupTable.html "IBusLookupTable"
[4]: https://ibus.github.io/docs/ibus-1.5/ibus-ibustypes.html "ibustypes"
