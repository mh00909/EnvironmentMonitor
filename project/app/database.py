import sqlite3
import bcrypt

# Funkcja do hashowania hasła
def hash_password(password):
    salt = bcrypt.gensalt()
    return bcrypt.hashpw(password.encode('utf-8'), salt)

# Funkcja do sprawdzania poprawności hasła
def check_password(password, hashed):
    return bcrypt.checkpw(password.encode('utf-8'), hashed)

# Funkcja inicjalizująca bazę danych
def init_db():
    conn = sqlite3.connect('environment_monitor.db')
    cursor = conn.cursor()

    # Tabela użytkowników
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        email TEXT UNIQUE NOT NULL,
        password TEXT NOT NULL
    )
    ''')

    # Tabela urządzeń
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS devices (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id INTEGER NOT NULL,
        device_id TEXT UNIQUE NOT NULL,
        status TEXT DEFAULT 'active',
        registration_date DATETIME DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
    )
    ''')


    # Tabela czujników
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS sensors (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id INTEGER NOT NULL,
        device_id TEXT NOT NULL,
        sensor_type TEXT NOT NULL,
        FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,
        FOREIGN KEY(device_id) REFERENCES devices(device_id) ON DELETE CASCADE
    )
    ''')

    # Tabela metryk
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS metrics (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id INTEGER NOT NULL,
        device_id TEXT NOT NULL,
        sensor_type TEXT NOT NULL,
        metric TEXT NOT NULL,
        FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,
        FOREIGN KEY(device_id) REFERENCES devices(device_id) ON DELETE CASCADE,
        FOREIGN KEY(sensor_type) REFERENCES sensors(sensor_type) ON DELETE CASCADE
    )
    ''')

    # Tabela subskrypcji
    cursor.execute('''
    CREATE TABLE IF NOT EXISTS subscriptions (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id INTEGER NOT NULL,
        topic TEXT NOT NULL,
        FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
    )
    ''')

    conn.commit()
    conn.close()

# Funkcja do uzyskania połączenia z bazą danych
def get_db_connection():
    conn = sqlite3.connect('environment_monitor.db')
    conn.row_factory = sqlite3.Row  # Umożliwia dostęp do kolumn po nazwach
    return conn

# Funkcja do dodania użytkownika
def add_user(email, password):
    hashed = hash_password(password)
    conn = get_db_connection()
    cursor = conn.cursor()

    try:
        cursor.execute('INSERT INTO users (email, password) VALUES (?, ?)', (email, hashed))
        conn.commit()
        print(f"User {email} added successfully.")
    except sqlite3.IntegrityError:
        print(f"User with email {email} already exists.")
    finally:
        conn.close()

# Funkcja do pobierania użytkownika po ID
def get_user_by_id(user_id):
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT * FROM users WHERE id = ?', (user_id,))
    user = cursor.fetchone()
    conn.close()
    return user

# Funkcja do pobierania tematów subskrypcji dla użytkownika
def get_user_topics(user_id):
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT topic FROM subscriptions WHERE user_id = ?', (user_id,))
    topics = [row['topic'] for row in cursor.fetchall()]
    conn.close()
    return topics

# Funkcja do dodania urządzenia
def add_device(user_id, device_id):
    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        cursor.execute('INSERT INTO devices (user_id, device_id) VALUES (?, ?)', (user_id, device_id))
        conn.commit()
        print(f"Device {device_id} added successfully for user {user_id}.")
    except sqlite3.IntegrityError as e:
        print(f"Error adding device: {e}")
    finally:
        conn.close()

# Funkcja do dodania czujnika
def add_sensor(user_id, device_id, sensor_type):
    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        cursor.execute(
            'INSERT INTO sensors (user_id, device_id, sensor_type) VALUES (?, ?, ?)',
            (user_id, device_id, sensor_type)
        )
        conn.commit()
        print(f"Sensor {sensor_type} added successfully for device {device_id}.")
    except sqlite3.IntegrityError as e:
        print(f"Error adding sensor: {e}")
    finally:
        conn.close()

# Funkcja do dodania metryki
def add_metric(user_id, device_id, sensor_type, metric):
    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        cursor.execute(
            'INSERT INTO metrics (user_id, device_id, sensor_type, metric) VALUES (?, ?, ?, ?)',
            (user_id, device_id, sensor_type, metric)
        )
        conn.commit()
        print(f"Metric {metric} added successfully for sensor {sensor_type}.")
    except sqlite3.IntegrityError as e:
        print(f"Error adding metric: {e}")
    finally:
        conn.close()

# Funkcja do dodania subskrypcji
def add_subscription(user_id, device_id, sensor_type, metric):
    topic = f"/{user_id}/{device_id}/{sensor_type}/{metric}"
    conn = get_db_connection()
    cursor = conn.cursor()
    try:
        cursor.execute(
            'INSERT INTO subscriptions (user_id, topic) VALUES (?, ?)',
            (user_id, topic)
        )
        conn.commit()
        print(f"Subscription added for topic: {topic}")
    except sqlite3.IntegrityError as e:
        print(f"Error adding subscription: {e}")
    finally:
        conn.close()










def get_user_devices(user_id):
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT device_id FROM devices WHERE user_id = ?', (user_id,))
    devices = [{"device_id": row["device_id"]} for row in cursor.fetchall()]
    conn.close()
    return devices


def get_device_sensors(user_id, device_id):
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT sensor_type FROM sensors WHERE user_id = ? AND device_id = ?', (user_id, device_id))
    sensors = [{"sensor_type": row["sensor_type"]} for row in cursor.fetchall()]
    conn.close()
    return sensors


def get_sensor_metrics(user_id, device_id, sensor_type):
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT metric FROM metrics WHERE user_id = ? AND device_id = ? AND sensor_type = ?', 
                   (user_id, device_id, sensor_type))
    metrics = [{"metric": row["metric"]} for row in cursor.fetchall()]
    conn.close()
    return metrics


def transfer_device(device_id, new_user_id):
    conn = get_db_connection()
    cursor = conn.cursor()

    try:
        # Pobierz aktualnego właściciela urządzenia
        cursor.execute('SELECT user_id FROM devices WHERE device_id = ?', (device_id,))
        result = cursor.fetchone()
        if not result:
            print(f"Device {device_id} does not exist.")
            return False

        old_user_id = result['user_id']

        # Zaktualizuj właściciela urządzenia
        cursor.execute(
            'UPDATE devices SET user_id = ?, status = ? WHERE device_id = ?',
            (new_user_id, 'transferred', device_id)
        )
        conn.commit()

        # Opcjonalne logowanie transferu
        cursor.execute(
            'INSERT INTO device_logs (device_id, action, old_user_id, new_user_id) VALUES (?, ?, ?, ?)',
            (device_id, 'transfer', old_user_id, new_user_id)
        )
        conn.commit()

        print(f"Device {device_id} successfully transferred from user {old_user_id} to user {new_user_id}.")
        return True
    except sqlite3.Error as e:
        print(f"Error transferring device: {e}")
        return False
    finally:
        conn.close()



def get_device_status(device_id):
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute('SELECT status FROM devices WHERE device_id = ?', (device_id,))
    result = cursor.fetchone()
    conn.close()

    if result:
        return result['status']
    return None
