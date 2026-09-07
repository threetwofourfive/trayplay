-- Neon Pong for TrayPlay (320x180 Virtual Console)

local paddle_w = 4
local paddle_h = 28
local paddle_speed = 3

local p1 = { x = 12, y = 76, score = 0 }
local p2 = { x = 304, y = 76, score = 0 }

local ball = {
    x = 160,
    y = 90,
    vx = 2.5,
    vy = 1.5,
    size = 4
}

local sound_muted = false
local last_m_btn = false

local function play_sound(name)
    if not sound_muted then
        sfx(name)
    end
end

function init()
    p1.y = 76
    p2.y = 76
    p1.score = 0
    p2.score = 0
    reset_ball(1)
end

function reset_ball(dir)
    ball.x = 160
    ball.y = 90
    ball.vx = 2.5 * dir
    ball.vy = (math.random() > 0.5 and 1.5 or -1.5)
end

function update(dt)
    -- Mute sound toggle (M key)
    local m_pressed = btn("m")
    if m_pressed and not last_m_btn then
        sound_muted = not sound_muted
        if sound_muted then
            stop_audio()
        else
            sfx("blip")
        end
    end
    last_m_btn = m_pressed

    -- 1. Player 1 input (Strictly manual: W/S or Up/Down arrows)
    if btn("up") or btn("w") then
        p1.y = math.max(4, p1.y - paddle_speed)
    elseif btn("down") or btn("s") then
        p1.y = math.min(180 - paddle_h - 4, p1.y + paddle_speed)
    end

    -- 2. AI Player 2 tracking
    local ai_center = p2.y + paddle_h / 2
    if ball.vx > 0 and ball.x > 100 then
        if ai_center < ball.y - 3 then
            p2.y = math.min(180 - paddle_h - 4, p2.y + paddle_speed * 0.85)
        elseif ai_center > ball.y + 3 then
            p2.y = math.max(4, p2.y - paddle_speed * 0.85)
        end
    end

    -- 3. Ball movement
    ball.x = ball.x + ball.vx
    ball.y = ball.y + ball.vy

    -- 4. Bounce top and bottom boundaries
    if ball.y <= 4 then
        ball.y = 4
        ball.vy = -ball.vy
        play_sound("wall")
    elseif ball.y >= 180 - ball.size - 4 then
        ball.y = 180 - ball.size - 4
        ball.vy = -ball.vy
        play_sound("wall")
    end

    -- 5. Collision with Left Paddle (p1)
    if ball.x <= p1.x + paddle_w and ball.x + ball.size >= p1.x then
        if ball.y + ball.size >= p1.y and ball.y <= p1.y + paddle_h then
            ball.x = p1.x + paddle_w + 1
            ball.vx = math.abs(ball.vx) * 1.05 -- speed up
            -- Angle deflection based on hit position
            local offset = (ball.y + ball.size / 2) - (p1.y + paddle_h / 2)
            ball.vy = offset * 0.15
            play_sound("hit")
        end
    end

    -- 6. Collision with Right Paddle (p2)
    if ball.x + ball.size >= p2.x and ball.x <= p2.x + paddle_w then
        if ball.y + ball.size >= p2.y and ball.y <= p2.y + paddle_h then
            ball.x = p2.x - ball.size - 1
            ball.vx = -math.abs(ball.vx) * 1.05 -- speed up
            local offset = (ball.y + ball.size / 2) - (p2.y + paddle_h / 2)
            ball.vy = offset * 0.15
            play_sound("hit")
        end
    end

    -- 7. Goal detection
    if ball.x < 0 then
        p2.score = p2.score + 1
        play_sound("score")
        reset_ball(1)
    elseif ball.x > 320 then
        p1.score = p1.score + 1
        play_sound("score")
        reset_ball(-1)
    end
end

function draw()
    -- Clear to dark blue (retro palette 1)
    cls(1)

    -- Field borders (top & bottom)
    rectfill(0, 0, 320, 3, 5) -- dark gray border
    rectfill(0, 177, 320, 3, 5)

    -- Center dividing net
    for y = 6, 174, 8 do
        rectfill(159, y, 2, 4, 5)
    end

    -- Subtitle
    print("PONG", 148, 6, 5, 1)

    -- Player 1 Score (Blue, 2x scale)
    print(p1.score, 110, 10, 12, 2)

    -- Player 2 Score (Red, 2x scale)
    print(p2.score, 196, 10, 8, 2)

    -- Left Paddle: Blue (palette 12)
    rectfill(p1.x, p1.y, paddle_w, paddle_h, 12)

    -- Right Paddle: Red (palette 8)
    rectfill(p2.x, p2.y, paddle_w, paddle_h, 8)

    -- Ball: Yellow (palette 10)
    rectfill(math.floor(ball.x), math.floor(ball.y), ball.size, ball.size, 10)

    -- Controls hints
    print("W / S : MOVE", 8, 166, 5, 1)

    local mute_label = sound_muted and "M : MUTE [ON]" or "M : MUTE [OFF]"
    local mute_color = sound_muted and 8 or 5 -- Red if muted, dark gray if sound on
    local mute_x = 320 - text_width(mute_label, 1) - 8
    print(mute_label, mute_x, 166, mute_color, 1)
end
