extends Control

const WORLD_SIZE := Vector2i(320, 240)
const DISPLAY_SCALE := 2
const SNAPSHOT_PATH := "user://sandworld_snapshot.bin"

var world := SandWorld.new()
var canvas: TextureRect
var debug_overlay: Control
var status_label: Label
var pause_button: Button
var brush_slider: HSlider
var debug_checkbox: CheckButton
var selected_material := 2
var paused := false
var drawing := false
var show_debug_overlay := false
var previous_world_position := Vector2i.ZERO
var image: Image
var texture: ImageTexture


func _ready() -> void:
	_configure_materials()
	_build_interface()
	_create_texture()
	_refresh_canvas()


func _process(_delta: float) -> void:
	if not paused:
		world.tick()
	_refresh_canvas()
	if show_debug_overlay:
		debug_overlay.queue_redraw()
	_update_status()


func _on_canvas_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			drawing = true
			previous_world_position = _to_world_position(canvas.get_local_mouse_position())
			_draw_to(previous_world_position)
		elif not event.pressed:
			drawing = false
	elif event is InputEventMouseMotion and drawing:
		var world_position := _to_world_position(canvas.get_local_mouse_position())
		_draw_line(previous_world_position, world_position)
		previous_world_position = world_position


func _configure_materials() -> void:
	world.add_material(1, "Stone", 1, Color("808080"), 100, 0, 0, 80)
	world.add_material(2, "Sand", 2, Color("e6d899"), 50, 0, 120, 120)
	world.add_material(3, "Water", 3, Color("3366cc"), 30, 4, 0, 0)
	world.add_material(4, "Acid", 3, Color("78d83d"), 45, 3, 0, 0)
	world.add_material(5, "Fire", 4, Color("ff6b21"), 1, 0, 0, 0, 40, 6)
	world.add_material(6, "Smoke", 4, Color("888888aa"), 1, 3, 0, 0, 3, 0)


func _build_interface() -> void:
	var layout := HBoxContainer.new()
	layout.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	layout.add_theme_constant_override("separation", 16)
	add_child(layout)

	var canvas_container := Control.new()
	canvas_container.custom_minimum_size = Vector2(WORLD_SIZE * DISPLAY_SCALE)
	layout.add_child(canvas_container)

	var canvas_background := ColorRect.new()
	canvas_background.color = Color("20242b")
	canvas_background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	canvas_background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	canvas_container.add_child(canvas_background)

	canvas = TextureRect.new()
	canvas.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	canvas.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	canvas.mouse_filter = Control.MOUSE_FILTER_STOP
	canvas.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	canvas.gui_input.connect(_on_canvas_input)
	canvas_container.add_child(canvas)

	debug_overlay = Control.new()
	debug_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	debug_overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	debug_overlay.draw.connect(_draw_debug_overlay)
	canvas_container.add_child(debug_overlay)

	var controls := VBoxContainer.new()
	controls.custom_minimum_size = Vector2(230, 0)
	controls.add_theme_constant_override("separation", 8)
	layout.add_child(controls)

	var title := Label.new()
	title.text = "SandWorld Sandbox"
	title.add_theme_font_size_override("font_size", 20)
	controls.add_child(title)

	var palette := GridContainer.new()
	palette.columns = 2
	controls.add_child(palette)
	for mat in [
		["Stone", 1], ["Sand", 2], ["Water", 3], ["Acid", 4], ["Fire", 5], ["Smoke", 6], ["Erase", 0],
	]:
		var button := Button.new()
		button.text = mat[0]
		button.pressed.connect(_select_material.bind(mat[1]))
		palette.add_child(button)

	var brush_label := Label.new()
	brush_label.text = "Brush radius"
	controls.add_child(brush_label)
	brush_slider = HSlider.new()
	brush_slider.min_value = 1
	brush_slider.max_value = 20
	brush_slider.step = 1
	brush_slider.value = 4
	controls.add_child(brush_slider)

	var clear_button := Button.new()
	clear_button.text = "Clear world"
	clear_button.pressed.connect(_clear_world)
	controls.add_child(clear_button)

	pause_button = Button.new()
	pause_button.text = "Pause"
	pause_button.pressed.connect(_toggle_pause)
	controls.add_child(pause_button)

	var step_button := Button.new()
	step_button.text = "Step once"
	step_button.pressed.connect(_step_once)
	controls.add_child(step_button)

	var save_button := Button.new()
	save_button.text = "Save snapshot"
	save_button.pressed.connect(_save_snapshot)
	controls.add_child(save_button)

	var load_button := Button.new()
	load_button.text = "Load snapshot"
	load_button.pressed.connect(_load_snapshot)
	controls.add_child(load_button)

	debug_checkbox = CheckButton.new()
	debug_checkbox.text = "Show chunks / dirty rects"
	debug_checkbox.toggled.connect(_on_debug_overlay_toggled)
	controls.add_child(debug_checkbox)

	status_label = Label.new()
	status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	controls.add_child(status_label)
	_update_status()


func _create_texture() -> void:
	image = Image.create_empty(WORLD_SIZE.x, WORLD_SIZE.y, false, Image.FORMAT_RGBA8)
	texture = ImageTexture.create_from_image(image)
	canvas.texture = texture


func _refresh_canvas() -> void:
	image.set_data(WORLD_SIZE.x, WORLD_SIZE.y, false, Image.FORMAT_RGBA8, world.render_to_texture(WORLD_SIZE, Vector2i.ZERO))
	texture.update(image)


func _on_debug_overlay_toggled(pressed: bool) -> void:
	show_debug_overlay = pressed
	debug_overlay.queue_redraw()


func _draw_debug_overlay() -> void:
	if not show_debug_overlay:
		return
	const CHUNK_COLOR := Color(1, 1, 1, 0.35)
	const DIRTY_COLOR := Color(1, 0.2, 0.2, 0.85)
	var display_rect := _get_canvas_display_rect()
	var display_scale := display_rect.size / Vector2(WORLD_SIZE)
	for entry: Dictionary in world.get_debug_chunk_info():
		var world_rect: Rect2i = entry["world_rect"]
		var screen_rect := Rect2(display_rect.position + Vector2(world_rect.position) * display_scale, Vector2(world_rect.size) * display_scale)
		debug_overlay.draw_rect(screen_rect, CHUNK_COLOR, false, 1.0)

		var dirty_rect: Rect2i = entry["dirty_rect"]
		if dirty_rect.size.x > 0 and dirty_rect.size.y > 0:
			var dirty_screen_rect := Rect2(display_rect.position + Vector2(dirty_rect.position) * display_scale, Vector2(dirty_rect.size) * display_scale)
			debug_overlay.draw_rect(dirty_screen_rect, DIRTY_COLOR, false, 2.0)


func _to_world_position(canvas_position: Vector2) -> Vector2i:
	var display_rect := _get_canvas_display_rect()
	var local := (canvas_position - display_rect.position) / display_rect.size * Vector2(WORLD_SIZE)
	return Vector2i(local).clamp(Vector2i.ZERO, WORLD_SIZE - Vector2i.ONE)


# STRETCH_KEEP_ASPECT_CENTERED letterboxes/centers the texture inside
# `canvas` whenever the control's actual size doesn't exactly match
# WORLD_SIZE * DISPLAY_SCALE (e.g. after a window resize). Mouse-to-world
# conversion and the debug overlay must both account for this offset and
# scale, rather than assuming canvas's origin/size map 1:1 to the texture.
func _get_canvas_display_rect() -> Rect2:
	var canvas_size := canvas.size
	var texture_size := Vector2(WORLD_SIZE)
	if canvas_size.x <= 0.0 or canvas_size.y <= 0.0:
		return Rect2(Vector2.ZERO, texture_size * DISPLAY_SCALE)
	var display_scale: float = min(canvas_size.x / texture_size.x, canvas_size.y / texture_size.y)
	var display_size := texture_size * display_scale
	var offset := (canvas_size - display_size) * 0.5
	return Rect2(offset, display_size)


func _draw_to(pos: Vector2i) -> void:
	world.brush_circle(pos, int(brush_slider.value), selected_material)
	_refresh_canvas()


func _draw_line(from: Vector2i, to: Vector2i) -> void:
	world.brush_line(from, to, selected_material, int(brush_slider.value))
	_refresh_canvas()


func _select_material(material_id: int) -> void:
	selected_material = material_id
	_update_status()


func _clear_world() -> void:
	world.clear()
	_refresh_canvas()
	_update_status("World cleared.")


func _toggle_pause() -> void:
	paused = not paused
	pause_button.text = "Resume" if paused else "Pause"
	_update_status()


func _step_once() -> void:
	world.tick()
	_refresh_canvas()
	_update_status("Advanced one tick.")


func _save_snapshot() -> void:
	var file := FileAccess.open(SNAPSHOT_PATH, FileAccess.WRITE)
	if file == null:
		_update_status("Unable to open the snapshot file for writing.")
		return
	file.store_buffer(world.save_snapshot())
	_update_status("Snapshot saved to user storage.")


func _load_snapshot() -> void:
	if not FileAccess.file_exists(SNAPSHOT_PATH):
		_update_status("No snapshot file exists yet.")
		return
	var file := FileAccess.open(SNAPSHOT_PATH, FileAccess.READ)
	if file == null:
		_update_status("Unable to open the snapshot file for reading.")
		return
	if world.load_snapshot(file.get_buffer(file.get_length())):
		_refresh_canvas()
		_update_status("Snapshot restored.")
	else:
		_update_status("Snapshot has an invalid or unsupported format.")


func _update_status(message := "") -> void:
	var material_names := ["Eraser", "Stone", "Sand", "Water", "Acid", "Fire", "Smoke"]
	var status := "Material: %s\nRadius: %d\nChunks: %d\nSimulation: %s" % [
		material_names[selected_material],
		int(brush_slider.value) if brush_slider else 4,
		world.get_chunk_count(),
		"paused" if paused else "running",
	]
	status_label.text = "%s\n%s" % [message, status] if not message.is_empty() else status
