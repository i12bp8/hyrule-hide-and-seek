#pragma once

namespace hs::ui {
// Scoped to this mod's window. Native widgets retain their touch, keyboard and controller actions.
inline constexpr const char* kWindowStyle = R"RCSS(
body { padding: 28dp; }
window {
    max-width: 1000dp;
    max-height: 760dp;
    border-radius: 14dp;
    border-width: 1dp;
    border-color: var(--color-border);
    background-color: rgba(var(--color-surface-rgb), 90%);
    color: var(--color-text);
}
window content pane {
    padding: 22dp;
    gap: 10dp;
    font-size: 17dp;
}
window tab-bar {
    font-size: 16dp;
    background-color: rgba(var(--color-neutral-rgb), 10%);
    border-bottom-width: 1dp;
    border-bottom-color: var(--color-border);
}
window tab-bar tab { padding: 0 20dp; opacity: 0.6; }
window tab-bar tab:selected { border-bottom-color: var(--color-accent); font-effect: none; }
window content pane.hs-nav { flex: 0 0 31%; }
window content pane.hs-detail { flex: 1 1 auto; }
window content pane.hs-main { flex: 1 1 70%; }
window content pane.hs-help {
    flex: 0 1 30%;
    font-size: 15dp;
    color: rgba(var(--color-text-rgb), 70%);
    background-color: rgba(var(--color-neutral-rgb), 5%);
}
window content pane.hs-help:empty { display: none; }
window content pane.hs-full { flex: 1 1 auto; }
window content pane.hs-unused { display: none; }
section-heading {
    font-size: 12dp;
    letter-spacing: 1dp;
    opacity: 1;
    color: rgba(var(--color-text-rgb), 55%);
}
section-heading:not(:first-of-type) { padding-top: 14dp; }
button, select-button {
    min-height: 46dp;
    padding: 10dp 14dp;
    border-radius: 8dp;
    background-color: var(--button-background);
    color: var(--color-text);
    box-shadow: none;
    opacity: 1;
}
button { font-size: 16dp; }
select-button key { font-size: 14dp; }
select-button value, select-button input { font-size: 16dp; }
button:not(:disabled):hover, select-button:not(:disabled):hover,
button:not(:disabled):focus-visible, select-button:not(:disabled):focus-visible {
    background-color: var(--button-background-hover);
    box-shadow: var(--color-accent) 0 0 0 2dp;
}
button.hs-primary {
    box-shadow: var(--color-accent) 0 0 0 1dp;
    font-weight: bold;
}
button.hs-primary:not(:disabled):hover, button.hs-primary:not(:disabled):focus-visible {
    background-color: var(--button-background-hover);
}
button.ui-list-row {
    text-align: left;
    font-size: 15dp;
    min-height: 46dp;
    white-space: normal;
}
ui-list-content { gap: 6dp; }
window content pane > ui-list {
    margin-left: 0;
    margin-right: 0;
    min-height: 120dp;
    flex: 1 0 120dp;
}
window content pane > ui-list ui-list-content,
window content pane > ui-list ui-list-empty { padding-left: 0; padding-right: 0; }
div.hs-muted, div.hs-status { color: rgba(var(--color-text-rgb), 65%); font-size: 14dp; line-height: 1.5; }
div.hs-code {
    font-size: 32dp;
    letter-spacing: 5dp;
    text-align: center;
    padding: 16dp;
    color: var(--color-text);
    background-color: rgba(var(--color-neutral-rgb), 10%);
    border-radius: 8dp;
}
.hs-brand { display: block; margin-bottom: 10dp; }
.hs-brand small { display: block; color: rgba(var(--color-text-rgb), 55%); font-size: 11dp; letter-spacing: 3dp; }
.hs-brand h1 { margin: 6dp 0 0 0; color: var(--color-text); font-size: 25dp; }
.hs-guide { display: block; padding: 14dp; background-color: rgba(var(--color-neutral-rgb), 10%); border-radius: 8dp; }
.hs-guide h3 { margin: 0 0 6dp 0; color: var(--color-text); font-size: 17dp; }
.hs-guide p { margin: 0; color: rgba(var(--color-text-rgb), 75%); font-size: 15dp; line-height: 1.5; }
@media (max-height: 520dp) {
    body { padding: 12dp; }
    window content pane { padding: 14dp; gap: 8dp; }
    window { max-height: 100%; }
}
@media (max-width: 640dp) {
    body { padding: 10dp; }
    window { max-width: 100%; max-height: 100%; }
    window content { flex-flow: column; }
    window content pane { padding: 14dp; }
    window content pane.hs-nav {
        flex: 0 0 auto;
        flex-flow: row wrap;
        gap: 8dp;
        border-right-width: 0;
        border-bottom-width: 1dp;
        border-bottom-color: var(--color-border);
    }
    pane.hs-nav section-heading, pane.hs-nav .hs-brand { display: none; }
    pane.hs-nav .hs-status { flex: 1 0 100%; }
    pane.hs-nav select-button.group-button { flex: 1 1 40%; }
    window content pane.hs-detail, window content pane.hs-main { flex: 1 1 auto; }
    window content pane.hs-help { display: none; }
    window tab-bar { font-size: 13dp; }
    window tab-bar tab { padding: 0 12dp; }
    div.hs-code { padding: 12dp; font-size: 28dp; }
}
)RCSS";
}
