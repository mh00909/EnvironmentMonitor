from flask import Blueprint, jsonify, redirect, render_template, request, session, url_for, flash
from app.database import get_db_connection, add_device, add_sensor, add_metric
from app.mqtt_handler import mqtt_client, publish_add_client, publish_add_metric, publish_add_device, publish_user_data, publish_add_sensor
import json

devices_bp = Blueprint('devices', __name__)


@devices_bp.route('/index', endpoint='index')
def index():
    return render_template('index.html')

@devices_bp.route('/devices/add-form', methods=['GET'])
def add_device_form():
    if 'user_id' not in session:
        flash("Musisz być zalogowany, aby dodać urządzenie.", "danger")
        return redirect(url_for('auth.login'))
    return render_template('add_device.html')

@devices_bp.route('/devices/add', methods=['POST'])
def add_device_route():
    if 'user_id' not in session:
        flash("Musisz być zalogowany, aby dodać urządzenie.", "danger")
        return redirect(url_for('auth.login'))

    user_id = session['user_id']
    device_id = request.form.get('device_id')

    if not device_id:
        flash("Musisz podać ID urządzenia.", "danger")
        return redirect(url_for('devices.add_device_form'))

    try:
        add_device(user_id, device_id)

        # Publikacja na MQTT
        topic_device = "/system/add_device"
        payload_device = json.dumps({"user_id": str(user_id), "device_id": device_id})
        mqtt_client.publish(topic_device, payload_device, retain=True)
        flash("Urządzenie zostało dodane pomyślnie.", "success")
    except Exception as e:
        flash(f"Błąd podczas dodawania urządzenia: {e}", "danger")

    return redirect(url_for('auth.account_settings'))


@devices_bp.route('/devices/add-sensor', methods=['GET'])
def add_sensor_form():
    if 'user_id' not in session:
        return redirect(url_for('auth.login'))

    return render_template('add_sensor.html')



@devices_bp.route('/devices/data', methods=['GET'])
def render_data():
    return render_template('data.html')


@devices_bp.route('/devices/add-sensor', methods=['POST'])
def add_sensor():
    if 'user_id' not in session:
        return redirect(url_for('auth.login'))

    user_id = session['user_id']
    device_id = request.form.get('device_id')
    sensor_id = request.form.get('sensor_id')
    metric_ids = request.form.get('metric_id').split(',')

    if not device_id or not sensor_id or not metric_ids:
        flash("Wszystkie pola są wymagane.", "danger")
        return redirect(url_for('devices.add_sensor_form'))

    # Publikuj czujnik
    publish_add_sensor(user_id, device_id, sensor_id)

    # Publikuj metryki
    for metric_id in metric_ids:
        publish_add_metric(user_id, device_id, sensor_id, metric_id.strip())
        topic = f"/{user_id}/{device_id}/{sensor_id}/{metric_id.strip()}"
        mqtt_client.subscribe(topic)

    flash("Czujnik i metryki zostały dodane.", "success")
    return redirect(url_for('devices.add_sensor_form'))
