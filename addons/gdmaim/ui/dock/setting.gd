@tool
extends Node


const _Settings := preload("../../settings.gd")

@export var settings_var : String
@export var node_var : String

var settings : _Settings


func set_settings(settings : _Settings) -> void:
	self.settings = settings
	deserialize()


func serialize() -> void:
	settings.set(settings_var, get(node_var))


func deserialize() -> void:
	#prints("deserialize", settings_var, settings.get(settings_var))
	set(node_var, settings.get(settings_var))
