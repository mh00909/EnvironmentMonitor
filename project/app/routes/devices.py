from flask import Blueprint, jsonify, redirect, render_template, request, session, url_for, flash
from app.database import get_db_connection, add_device, add_sensor, add_metric
from app.mqtt_handler import mqtt_client, publish_add_client, publish_add_metric, publish_add_device, publish_user_data, publish_add_sensor
import json

devices_bp = Blueprint('devices', __name__)
@devices_bp.route('/index', methods=['GET'])
def index():
    if 'user_id' not in session:
        return redirect(url_for('auth.login'))

    user_id = session['user_id']
    conn = get_db_connection()
    cursor = conn.cursor()

    # Pobierz email użytkownika
    cursor.execute('SELECT email FROM users WHERE id = ?', (user_id,))
    user_email = cursor.fetchone()['email']

    # Pobierz urządzenia użytkownika
    cursor.execute('SELECT device_id FROM devices WHERE user_id = ?', (user_id,))
    devices = [row['device_id'] for row in cursor.fetchall()]

    # Pobierz czujniki i metryki dla każdego urządzenia
    sensors = {}
    for device in devices:
        cursor.execute('SELECT sensor_type FROM sensors WHERE user_id = ? AND device_id = ?', (user_id, device))
        sensors[device] = {}
        for row in cursor.fetchall():
            sensor_type = row['sensor_type']
            cursor.execute(
                'SELECT metric FROM metrics WHERE user_id = ? AND device_id = ? AND sensor_type = ?',
                (user_id, device, sensor_type)
            )
            sensors[device][sensor_type] = [metric_row['metric'] for metric_row in cursor.fetchall()]

    # Debugowanie - wydrukuj strukturę danych
    print("Devices:", devices)
    print("Sensors:", sensors)

    # Pobierz subskrybowane tematy
    cursor.execute('SELECT topic FROM subscriptions WHERE user_id = ?', (user_id,))
    subscriptions = [row['topic'] for row in cursor.fetchall()]

    conn.close()

    return render_template(
        'index.html',
        email=user_email,
        devices=devices,
        sensors=sensors,
        subscriptions=subscriptions
    )


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
    if 'user_id' not in session:
        return redirect(url_for('auth.login'))

    user_id = session['user_id']
    conn = get_db_connection()
    cursor = conn.cursor()

    # Pobierz urządzenia użytkownika
    cursor.execute('SELECT device_id FROM devices WHERE user_id = ?', (user_id,))
    devices = [row['device_id'] for row in cursor.fetchall()]

    # Pobierz czujniki i metryki dla każdego urządzenia
    user_sensors = {}
    for device in devices:
        cursor.execute('SELECT sensor_type FROM sensors WHERE user_id = ? AND device_id = ?', (user_id, device))
        user_sensors[device] = {}
        for row in cursor.fetchall():
            sensor_type = row['sensor_type']
            cursor.execute(
                'SELECT metric FROM metrics WHERE user_id = ? AND device_id = ? AND sensor_type = ?',
                (user_id, device, sensor_type)
            )
            user_sensors[device][sensor_type] = [metric_row['metric'] for metric_row in cursor.fetchall()]

    sensor_labels = {
        "ble": "Termometr BLE",
        "bmp280": "Czujnik BMP280",
        "temperature": "Temperatura",
        "photoresistor": "Fotorezystor"
    }
    metric_labels = {
        "temperature": "🌡️ Temperatura",
        "humidity": "💧 Wilgotność",
        "light": "💡 Natężenie światła",
        "pressure": "💨 Ciśnienie atmosferyczne"
    }

    conn.close()
    return render_template('data.html', user_sensors=user_sensors, sensor_labels=sensor_labels, metric_labels=metric_labels)


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

    # Zapisz czujnik do bazy
    try:
        conn = get_db_connection()
        cursor = conn.cursor()

        # Dodaj czujnik
        cursor.execute(
            'INSERT INTO sensors (user_id, device_id, sensor_type) VALUES (?, ?, ?)',
            (user_id, device_id, sensor_id)
        )

        # Dodaj metryki
        for metric_id in metric_ids:
            cursor.execute(
                'INSERT INTO metrics (user_id, device_id, sensor_type, metric) VALUES (?, ?, ?, ?)',
                (user_id, device_id, sensor_id, metric_id.strip())
            )

        conn.commit()
        conn.close()

        # Publikuj dane przez MQTT
        publish_add_sensor(user_id, device_id, sensor_id)
        for metric_id in metric_ids:
            publish_add_metric(user_id, device_id, sensor_id, metric_id.strip())
            topic = f"/{user_id}/{device_id}/{sensor_id}/{metric_id.strip()}"
            mqtt_client.subscribe(topic)

        flash("Czujnik i metryki zostały dodane.", "success")
    except Exception as e:
        flash(f"Błąd podczas dodawania czujnika i metryk: {e}", "danger")

    return redirect(url_for('devices.add_sensor_form'))




@devices_bp.route('/devices/register', methods=['POST'])
def register_device():
    if 'user_id' not in session:
        return jsonify({'error': 'Musisz być zalogowany'}), 401

    user_id = session['user_id']
    device_id = request.json.get('device_id')

    if not device_id:
        return jsonify({'error': 'ID urządzenia jest wymagane'}), 400

    conn = get_db_connection()
    cursor = conn.cursor()

    # Sprawdzenie, czy urządzenie jest już zarejestrowane
    cursor.execute('SELECT user_id, status FROM devices WHERE device_id = ?', (device_id,))
    device = cursor.fetchone()

    if device:
        current_owner = device['user_id']
        if current_owner == user_id:
            return jsonify({'message': 'Urządzenie już jest przypisane do Twojego konta'}), 200

        # Przeniesienie urządzenia do nowego właściciela
        cursor.execute('UPDATE devices SET user_id = ?, status = ? WHERE device_id = ?',
                       (user_id, 'transferred', device_id))
        conn.commit()

        cursor.execute('INSERT INTO device_logs (device_id, action, old_user_id, new_user_id) VALUES (?, ?, ?, ?)',
                       (device_id, 'transfer', current_owner, user_id))
        conn.commit()

        conn.close()
        return jsonify({'message': 'Urządzenie zostało przeniesione na Twoje konto'}), 200
    else:
        # Rejestracja nowego urządzenia
        cursor.execute('INSERT INTO devices (device_id, user_id, status) VALUES (?, ?, ?)',
                       (device_id, user_id, 'active'))
        conn.commit()
        conn.close()
        return jsonify({'message': 'Urządzenie zostało zarejestrowane'}), 201



@devices_bp.route('/devices/list', methods=['GET'])
def list_user_devices():
    if 'user_id' not in session:
        return jsonify({'error': 'Musisz być zalogowany'}), 401

    user_id = session['user_id']
    conn = get_db_connection()
    cursor = conn.cursor()

    cursor.execute('SELECT device_id, status FROM devices WHERE user_id = ?', (user_id,))
    devices = cursor.fetchall()
    conn.close()

    return jsonify(devices)



@devices_bp.route('/devices/transfer-form', methods=['GET'])
def transfer_device_form():
    if 'user_id' not in session:
        return redirect(url_for('auth.login'))

    user_id = session['user_id']
    conn = get_db_connection()
    cursor = conn.cursor()

    # Pobierz urządzenia użytkownika
    cursor.execute('SELECT device_id FROM devices WHERE user_id = ?', (user_id,))
    user_devices = [row['device_id'] for row in cursor.fetchall()]
    conn.close()

    return render_template('transfer_device.html', user_devices=user_devices)


@devices_bp.route('/devices/transfer', methods=['POST'])
def transfer_device():
    if 'user_id' not in session:
        return redirect(url_for('auth.login'))

    user_id = session['user_id']
    device_id = request.form.get('device_id')
    new_owner_email = request.form.get('new_owner_email')

    if not device_id or not new_owner_email:
        flash("Wszystkie pola są wymagane.", "danger")
        return redirect(url_for('devices.transfer_device_form'))

    conn = get_db_connection()
    cursor = conn.cursor()

    try:
        # Sprawdź, czy nowy właściciel istnieje
        cursor.execute('SELECT id FROM users WHERE email = ?', (new_owner_email,))
        new_owner = cursor.fetchone()

        if not new_owner:
            flash("Nowy właściciel nie istnieje.", "danger")
            return redirect(url_for('devices.transfer_device_form'))

        new_owner_id = new_owner['id']

        # Przenieś urządzenie do nowego właściciela
        cursor.execute('UPDATE devices SET user_id = ? WHERE user_id = ? AND device_id = ?',
                       (new_owner_id, user_id, device_id))

        # Przenieś czujniki powiązane z urządzeniem
        cursor.execute('UPDATE sensors SET user_id = ? WHERE user_id = ? AND device_id = ?',
                       (new_owner_id, user_id, device_id))

        # Przenieś metryki powiązane z urządzeniem
        cursor.execute('UPDATE metrics SET user_id = ? WHERE user_id = ? AND device_id = ?',
                       (new_owner_id, user_id, device_id))

        cursor.execute('DELETE FROM subscriptions WHERE user_id = ? AND topic LIKE ?',
                       (user_id, f"/{user_id}/{device_id}/%"))

        # Dodaj nowe subskrypcje dla nowego właściciela
        cursor.execute('SELECT sensor_type FROM sensors WHERE user_id = ? AND device_id = ?', (new_owner_id, device_id))
        sensors = cursor.fetchall()

        for sensor in sensors:
            sensor_type = sensor['sensor_type']
            cursor.execute(
                'SELECT metric FROM metrics WHERE user_id = ? AND device_id = ? AND sensor_type = ?',
                (new_owner_id, device_id, sensor_type)
            )
            metrics = cursor.fetchall()

            for metric in metrics:
                topic = f"/{new_owner_id}/{device_id}/{sensor_type}/{metric['metric']}"
                cursor.execute('INSERT INTO subscriptions (user_id, topic) VALUES (?, ?)', (new_owner_id, topic))

        conn.commit()

        flash(f"Urządzenie {device_id} oraz powiązane czujniki i metryki zostały przeniesione do {new_owner_email}.", "success")
    except Exception as e:
        flash(f"Błąd podczas przenoszenia urządzenia: {e}", "danger")
    finally:
        conn.close()

    return redirect(url_for('auth.account_settings'))
