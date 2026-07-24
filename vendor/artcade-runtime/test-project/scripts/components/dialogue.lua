-- =============================================================================
-- DialogueSystem — Logic Component
-- Manages branching text conversations loaded from a table.
--
-- Conversation format:
--   {
--       id    = "intro",
--       nodes = {
--           { text = "Hello!" },                        -- plain line (auto-advance)
--           { text = "Pick:", choices = {
--               { text = "Option A", next = 3 },        -- next = absolute node index
--               { text = "Option B", next = 4 },
--           }},
--           { text = "Chose A." },
--           { text = "Chose B." },
--       }
--   }
--
-- Usage:
--   local ds = DialogueSystem.new({ intro = introConv })
--   ds:start("intro")       -- begin a conversation by id
--   ds:advance()            -- move to next node (noop when choices present)
--   ds:choose(index)        -- pick a 1-based choice at the current node
--   ds:getCurrentLine()     -- text of the current node, or nil
--   ds:getChoices()         -- table of choices, or nil
--   ds:isActive()           -- true while a conversation is running
--   ds:draw(x, y)           -- render current text via debug.drawText
-- =============================================================================

DialogueSystem = {}
DialogueSystem.__index = DialogueSystem

function DialogueSystem.new(conversations)
    local self = setmetatable({}, DialogueSystem)
    self._convs   = conversations or {}
    self._conv    = nil   -- active conversation table
    self._nodeIdx = 0     -- current 1-based node index
    self._active  = false
    return self
end

-- Begin a conversation by id key.
function DialogueSystem:start(id)
    local c = self._convs[id]
    if not c then return end
    self._conv    = c
    self._nodeIdx = 1
    self._active  = true
    if event and event.emit then
        event.emit("dialogue.started", { id = id })
    end
end

-- Current node text string (nil when inactive or out of range).
function DialogueSystem:getCurrentLine()
    if not self._active or not self._conv then return nil end
    local node = self._conv.nodes[self._nodeIdx]
    return node and node.text
end

-- Current node choices table, or nil when none / inactive.
function DialogueSystem:getChoices()
    if not self._active or not self._conv then return nil end
    local node = self._conv.nodes[self._nodeIdx]
    if not node or not node.choices or #node.choices == 0 then return nil end
    return node.choices
end

-- Move to the next sequential node.
-- Ignored when the current node has choices (caller must use choose()).
function DialogueSystem:advance()
    if not self._active then return end
    local node = self._conv.nodes[self._nodeIdx]
    if not node then self:_end(); return end
    if node.choices and #node.choices > 0 then return end   -- must choose
    self._nodeIdx = self._nodeIdx + 1
    if self._nodeIdx > #self._conv.nodes then
        self:_end()
    end
end

-- Pick choice at 1-based index; jumps to choice.next or the sequential node.
function DialogueSystem:choose(index)
    if not self._active then return end
    local node = self._conv.nodes[self._nodeIdx]
    if not node or not node.choices then return end
    local choice = node.choices[index]
    if not choice then return end
    if choice.next then
        self._nodeIdx = choice.next
    else
        self._nodeIdx = self._nodeIdx + 1
    end
    if self._nodeIdx > #self._conv.nodes then
        self:_end()
    end
end

-- True while a conversation is active.
function DialogueSystem:isActive()
    return self._active
end

-- Render current line and choices using debug.drawText.
function DialogueSystem:draw(x, y)
    if not self._active then return end
    local line = self:getCurrentLine()
    if line and debug and debug.drawText then
        debug.drawText(line, x, y, 20, "white")
    end
    local choices = self:getChoices()
    if choices and debug and debug.drawText then
        for i, ch in ipairs(choices) do
            debug.drawText(tostring(i) .. ". " .. ch.text, x, y + i * 24, 16, "yellow")
        end
    end
end

-- Internal: end the active conversation and emit event.
function DialogueSystem:_end()
    local id = self._conv and self._conv.id
    self._active  = false
    self._conv    = nil
    self._nodeIdx = 0
    if event and event.emit then
        event.emit("dialogue.ended", { id = id })
    end
end
