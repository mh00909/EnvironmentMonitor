from flask import Blueprint, request, redirect, render_template, session, url_for, flash
from app.database import add_subscription, get_db_connection, hash_password, check_password
from app.mqtt_handler import mqtt_client, publish_add_client, publish_add_device, subscribe_to_metrics, publish_add_metric, publish_user_data, setup_mqtt
import sqlite3
import json

auth_bp = Blueprint('auth', __name__)

# Rejestracja nowego użytkownika
@auth_bp.route('/register', methods=['GET', 'POST'])
def register():
    if request.method == 'POST':
        email = request.form.get('email')
        password = request.form.get('password')

        # Walidacja danych wejściowych
        if not email or not password:
            flash("Email and password are required", "danger")
            return render_template('register.html')

        hashed_password = hash_password(password)
        conn = get_db_connection()
        if conn is None:
            flash("Database connection error", "danger")
            return render_template('register.html')

        cursor = conn.cursor()

        try:
            # Dodanie nowego użytkownika do bazy danych
            cursor.execute('INSERT INTO users (email, password) VALUES (?, ?)', (email, hashed_password))
            conn.commit()

            # Pobranie ID nowego użytkownika
            user_id = cursor.lastrowid

            # Publikacja użytkownika i urządzenia przez MQTT
            publish_add_client(user_id)

            flash("User registered successfully!", "success")
            return redirect(url_for('auth.login'))
        except sqlite3.IntegrityError:
            flash("Email already exists", "danger")
            return render_template('register.html')
        finally:
            conn.close()

    return render_template('register.html')


@auth_bp.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        email = request.form.get('email')
        password = request.form.get('password')

        if not email or not password:
            flash("Email and password are required", "danger")
            return render_template('login.html')

        conn = get_db_connection()
        cursor = conn.cursor()

        # Sprawdzenie użytkownika
        cursor.execute('SELECT id, password FROM users WHERE email = ?', (email,))
        user = cursor.fetchone()
        conn.close()

        if user and check_password(password, user['password']):
            session['user_id'] = user['id']

            # Publikacja na temat /system/add_client
            publish_add_client(user['id'])
            # Pobranie urządzeń, czujników i metryk
            publish_user_data(user['id'])
            subscribe_to_metrics(user['id'])
            flash("Login successful!", "success")
            return redirect(url_for('devices.index'))  # Przejście na stronę główną
        else:
            flash("Invalid email or password", "danger")
            return render_template('login.html')

    return render_template('login.html')



# Wylogowanie użytkownika
@auth_bp.route('/logout')
def logout():
    session.pop('user_id', None)
    mqtt_client.loop_stop()
    mqtt_client.disconnect()
    flash("You have been logged out.", "info")
    return redirect(url_for('auth.login'))













@auth_bp.route('/account/settings', methods=['GET'])
def account_settings():
    if 'user_id' not in session:
        flash("Musisz być zalogowany, aby uzyskać dostęp do ustawień konta.", "danger")
        return redirect(url_for('auth.login'))

    user_id = session['user_id']
    conn = get_db_connection()
    cursor = conn.cursor()

    # Pobierz urządzenia użytkownika
    cursor.execute('SELECT device_id FROM devices WHERE user_id = ?', (user_id,))
    user_devices = [row['device_id'] for row in cursor.fetchall()]

    # Pobierz subskrypcje
    cursor.execute('SELECT topic FROM subscriptions WHERE user_id = ?', (user_id,))
    user_subscriptions = [row['topic'] for row in cursor.fetchall()]

    conn.close()

    return render_template(
        'account_settings.html',
        email="user@example.com",
        user_devices=user_devices,
        user_subscriptions=user_subscriptions,
        available_devices=['ESP32-01', 'ESP32-02'],
        selected_devices=[],
        available_sensors=['bmp280', 'photoresistor', 'ble'],
        selected_sensors=[],
        available_metrics=['temperature', 'humidity', 'light'],
        selected_metrics=[]
    )
