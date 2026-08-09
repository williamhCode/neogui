# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Add icon to app
- Add session_edit command
- Add automatic font fallback
- Add proper emoji rendering behavior, adheres to Unicode standard
- Add cmd argument to session_restart
- Add ligature rendering yay!! (this one has been put off for a while haha, also fixes some emojis)
- Add font\_features option
- Add session_vim_cmd command to execute a vim command in any session

### Changed
- Use compindex for floating window (nvim 0.12+)
- Simplify msg window and ime window rendering logic
- Emoji renders on top of cursor instead of using alpha mask
- Use CoreText instead of Freetype for text rendering
- session_restart uses nvim_exec2 instead of exec_lua (error messages now match session_vim_cmd)
- Neogurt command errors are displayed with vim.notify and return nil instead of raising

### Fixed
- FontFamily::UpdateLinespace not setting topLinespace
- Bundle SF Mono (default font) so no crashing if system doesn't have SF Mono
- Fix dpiScale not updating for fonts
- Proper TextureAtlas resizing (wasn't clearing all the glyph info maps before leading
to rendering artifacts)
- Fix rendering whitespace characters causing repeated font creation
- Fix underline rendering cutting off sometimes (made it so underline always renders within cell)
- Fix keyboard input: F13-F24 and other extended keys now work, emoji input via picker supported
- Fix purple flickering bug (maybe lol)
- Fix titlebar issue when toggling fullscreen
- Prevent background session renames from changing the window title

## [0.2.2] - 2025-05-08

### Fixed
- `Client::DoRead()` crashing due to use after free error (0.2.1 was incorrect fix)

## [0.2.1] - 2025-05-05 [YANKED]

### Fixed
- `Client::DoRead()` crashing due to bad conversion

## [0.2.0] - 2025-05-05

### Added
- Emoji Rendering for joined emojis
- Options can be set dynamically

### Changed
- Rewrote startup code to be async

### Fixed
- Texture atlas blowing up
- A lot of stuff

> Previous changes before 0.2.0 were untracked in this changelog
