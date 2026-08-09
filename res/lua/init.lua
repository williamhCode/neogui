vim.g.neogurt = true
vim.g.neogurt_startup = function() end

local autocmd = vim.api.nvim_create_autocmd
autocmd("VimEnter", {
  callback = function()
    vim.rpcnotify(1, "vim_enter")
  end
})
autocmd("ColorScheme", {
  callback = function()
    vim.rpcnotify(1, "color_scheme")
  end
})
-- autocmd("InsertCharPre", {
--   callback = function()
--     vim.rpcnotify(1, "insert_char_pre")
--   end
-- })

local utils = require("utils")

-- if value is a type name, the option is required
local cmds_table = {
  -- sets neogurt options
  option_set = {
    -- global options (shared across all sessions)
    show_title = "boolean",
    titlebar = "string",

    blur = "number",
    gamma = "number",
    vsync = "boolean",
    fps = "number",

    margin_top = "number",
    margin_bottom = "number",
    margin_left = "number",
    margin_right = "number",

    macos_option_is_meta = "string",
    cursor_idle_time = "number",
    scroll_speed = "number",

    post_processing = "boolean",

    -- session specific options
    bg_color = "number",
    opacity = "number",

    font_features = "table",
  },

  -- returns session id
  session_new = {
    name = "",
    dir = "~/",
    switch_to = true, -- switch to session after creating it
  },
  -- returns success (bool)
  session_edit = {
    id = 0,
    name = "",
  },
  -- returns success (bool)
  session_kill = {
    id = 0
  },
  -- returns new session id
  session_restart = {
    id = 0,
    -- use cmd as the kill command (e.g. qall)
    -- if empty, will force kill session (like session_kill)
    -- if cmd returns an error when run, restart will not happen
    cmd = "",
    curr_dir = false, -- use cwd instead of session dir
  },
  -- returns success (bool)
  session_switch = {
    id = "number"
  },
  -- switch to previous session
  -- returns success (bool)
  session_prev = {},
  -- returns session info table (id, name, dir)
  session_info = {
    id = 0
  },
  -- returns list of session info tables
  session_list = {
    sort = "id",  -- id, name, time (recency)
    reverse = false,
  },
  -- spawns a session picker using vim.ui.select()
  -- wrapper around session_list
  session_select = {
    sort = "id",
    reverse = false,
  },
  -- executes a vim command in the target session
  -- returns the command output ("" if none), or vim.NIL if the session
  -- disconnects while executing (e.g. a successful qa)
  -- raises an error if the command fails or the id is invalid
  session_vim_cmd = {
    id = 0,
    cmd = "string",
  },

  font_size_change = {
    [1] = "number",
    all = false,  -- change in all sessions
  },
  font_size_reset = {
    all = false,  -- change in all sessions
  }
}

vim.api.nvim_create_user_command("Neogurt", function(cmd_opts)
  local cmd, opts = utils.convert_command_args(cmd_opts)
  if cmd == nil then return end

  -- convert strings to correct types
  local default_opts = cmds_table[cmd]
  if default_opts == nil then
    vim.api.nvim_err_writeln("Invalid command: " .. cmd)
    return
  end

  if utils.convert_opts(default_opts, opts) == false then
    return
  end

  -- print command output
  local ok, result = pcall(vim.g.neogurt_cmd, cmd, opts)
  if not ok then
    vim.api.nvim_err_writeln(result)
    return
  end
  if result ~= vim.NIL then
    vim.print(result)
  end

end, { nargs = "*" })

--- Sends a session command to neogurt
--- @param cmd string: command to send.
--- @param opts table|nil: command options
--- @return any: result of the command
vim.g.neogurt_cmd = function(cmd, opts)
  local chan_id = utils.get_neogurt_channel()
  if chan_id == nil then return end

  local default_opts = cmds_table[cmd]
  if default_opts == nil then
    vim.api.nvim_err_writeln("Invalid command: " .. cmd)
    return
  end
  -- merge + check required options and option types
  opts = opts or {}
  -- option_set is a special case (more like a map of values)
  if (cmd ~= "option_set") then
    opts = utils.merge_opts(default_opts, opts)
  end
  if opts == nil then return end

  -- convert positional keys to named keys (arg1, arg2...)
  opts = utils.convert_positional_keys(opts)

  if cmd == "session_new" then
    opts.dir = vim.fn.fnamemodify(opts.dir, ":p")
    if vim.fn.isdirectory(opts.dir) == 0 then
      vim.api.nvim_err_writeln("Directory does not exist: " .. opts.dir)
      return
    end

  elseif cmd == "session_select" then
    local curr_id = vim.g.neogurt_cmd("session_info").id
    local list = vim.g.neogurt_cmd("session_list", opts)
    vim.ui.select(list, {
      prompt = "Select a session",
      format_item = function(item)
        local line = item.name
        if item.id == curr_id then
          line = line .. " (current)"
        end
        return line
      end
    }, function(choice)
      if choice == nil then return end
      vim.g.neogurt_cmd("session_switch", { id = choice.id })
    end)
    return
  end

  if next(opts) == nil then
    return vim.rpcrequest(chan_id, "neogurt_cmd", cmd)
  else
    return vim.rpcrequest(chan_id, "neogurt_cmd", cmd, opts)
  end
end

vim.api.nvim_set_hl(0, "NeogurtImeNormal", { link = "Normal" })
vim.api.nvim_set_hl(0, "NeogurtImeSelected", { link = "Underlined" })
