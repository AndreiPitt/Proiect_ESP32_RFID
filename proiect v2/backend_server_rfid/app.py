from flask import Flask, request, jsonify, render_template, redirect, url_for
from flask_sqlalchemy import SQLAlchemy
from datetime import datetime, timezone, timedelta
import os
import pytz

# --- FLASK AND DB SETUP ---
app = Flask(__name__)

basedir = os.path.abspath(os.path.dirname(__file__))
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///' + os.path.join(basedir, 'rfid_app.db')
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
app.config['SECRET_KEY'] = 'a_super_secret_key_for_flask_forms'

db = SQLAlchemy(app)

SCAN_COOLDOWN_SECONDS = 300

# --- TIMEZONE SETUP ---
TARGET_TIMEZONE = pytz.timezone('Europe/Bucharest')


# --- MODEL DEFINITIONS ---
class Person(db.Model):
    __tablename__ = 'person'
    id = db.Column(db.Integer, primary_key=True)
    uid_card = db.Column(db.String(50), unique=True, nullable=False)
    first_name = db.Column(db.String(100), nullable=False)
    last_name = db.Column(db.String(100), nullable=False)
    is_inside = db.Column(db.Boolean, default=False)
    last_action_time = db.Column(db.DateTime, default=lambda: datetime(2025, 2, 14, tzinfo=timezone.utc))
    logs = db.relationship('Log', backref='person', lazy=True)


class Log(db.Model):
    __tablename__ = 'log'
    id = db.Column(db.Integer, primary_key=True)
    person_id = db.Column(db.Integer, db.ForeignKey('person.id'), nullable=False)
    timestamp = db.Column(db.DateTime, default=lambda: datetime.now(timezone.utc))
    action = db.Column(db.String(3), nullable=False)


# --- UTILITY FUNCTIONS ---
def initialize_db():
    db.create_all()


def localize_timestamp(timestamp):
    """Convertește un datetime (presupus UTC) în fusul orar local (TARGET_TIMEZONE)."""
    if timestamp.tzinfo is None:
        utc_dt = timestamp.replace(tzinfo=pytz.utc)
    else:
        utc_dt = timestamp.astimezone(pytz.utc)
    return utc_dt.astimezone(TARGET_TIMEZONE)


def calculate_session_durations(logs):
    sessions = []
    current_session_start = None
    now_utc = datetime.now(timezone.utc)

    for log in logs:
        log_timestamp = log.timestamp
        if log_timestamp.tzinfo is None:
            log_timestamp = log_timestamp.replace(tzinfo=timezone.utc)

        if log.action == 'IN':
            if current_session_start is not None:
                continue
            current_session_start = log_timestamp

        elif log.action == 'OUT':
            if current_session_start is not None:
                duration = log_timestamp - current_session_start
                sessions.append({
                    'start': current_session_start,
                    'end': log_timestamp,
                    'duration': duration,
                })
                current_session_start = None

    if current_session_start is not None:
        duration_so_far = now_utc - current_session_start
        sessions.append({
            'start': current_session_start,
            'end': None,
            'duration': duration_so_far,
            'is_current': True
        })

    return sorted(sessions, key=lambda x: x['start'], reverse=True)


def format_timedelta(td):
    """Formatează un obiect timedelta într-un șir de tipul Xh Ym Zs."""
    if td is None: return "N/A"
    total_seconds = int(td.total_seconds())
    if total_seconds < 0: total_seconds = 0

    hours, remainder = divmod(total_seconds, 3600)
    minutes, seconds = divmod(remainder, 60)

    parts = []
    if hours > 0: parts.append(f"{hours}h")
    if minutes > 0 or (hours > 0 and seconds >= 0): parts.append(f"{minutes}m")
    if seconds > 0 or total_seconds == 0: parts.append(f"{seconds}s")

    if not parts: return "0s"
    return " ".join(parts)


# --- ÎNREGISTRARE FILTRE JINJA (Global) ---
app.jinja_env.globals.update(
    localize_timestamp=localize_timestamp,
    format_timedelta=format_timedelta,
    TARGET_TIMEZONE_NAME=TARGET_TIMEZONE.zone
)


# --- RUTA API: /scan/<card_uid> ---
@app.route('/scan/<string:card_uid>', methods=['GET'])
def scan_rfid(card_uid):
    person = Person.query.filter_by(uid_card=card_uid).first()
    now_utc = datetime.now(timezone.utc)

    # 1. Card neînregistrat - Returnează 403, ESP32 poate interpreta și afișa linkul.
    if not person:
        return jsonify({
            'message': 'Card neinregistrat!',
            'card_uid': card_uid,
            'action': 'REGISTER_REQUIRED'
        }), 403

    # 2. Logica Cooldown (Anti-Spam)
    last_action_time = person.last_action_time
    if last_action_time.tzinfo is None:
        last_action_time = last_action_time.replace(tzinfo=timezone.utc)

    time_since_last_action = now_utc - last_action_time

    if time_since_last_action.total_seconds() < SCAN_COOLDOWN_SECONDS:
        remaining_seconds = int(SCAN_COOLDOWN_SECONDS - time_since_last_action.total_seconds())
        return jsonify({
            'message': f'Scanati prea repede! Asteptati inca {remaining_seconds} secunde!',
            'status': 'COOLDOWN'
        }), 429

    # 3. Determinare Acțiune și Update Stare
    if person.is_inside:
        new_action = 'OUT'
        person.is_inside = False
    else:
        new_action = 'IN'
        person.is_inside = True

    # 4. Salvare în Baza de Date
    person.last_action_time = now_utc
    new_log = Log(person_id=person.id, action=new_action)

    db.session.add(new_log)
    db.session.commit()

    return jsonify({
        'message': f'Scanare OK. Actiune: {new_action}. {person.first_name} {person.last_name}',
        'status': 'SUCCESS',
        'person_id': person.id
    }), 200


# --- RUTA ADMIN: Înregistrare Persoană (Admin) ---
@app.route('/register', methods=['GET', 'POST'])
def register_person():
    if request.method == 'POST':
        card_uid = request.form.get('uid_card').strip().upper()
        first_name = request.form.get('first_name').strip()
        last_name = request.form.get('last_name').strip()

        if not card_uid or not first_name or not last_name:
            return render_template('register.html', error_message="Toate câmpurile sunt obligatorii."), 400

        if Person.query.filter_by(uid_card=card_uid).first():
            return render_template('register.html', error_message=f"UID card ({card_uid}) deja înregistrat!"), 400

        new_person = Person(uid_card=card_uid, first_name=first_name, last_name=last_name)
        db.session.add(new_person)
        db.session.commit()

        return redirect(url_for('home', message_success=f'Persoana {last_name} a fost înregistrată cu succes.'))

    return render_template('register.html')


# --- RUTA STUDENT: Înregistrare Publică (Cu Autocomplete UID) (NOUĂ) ---
@app.route('/student_register', methods=['GET', 'POST'])
def student_register():
    # 1. Gestionarea Cererii POST (Salvarea datelor)
    if request.method == 'POST':
        card_uid = request.form.get('uid_card').strip().upper()
        first_name = request.form.get('first_name').strip()
        last_name = request.form.get('last_name').strip()

        if not card_uid or not first_name or not last_name:
            return render_template('student_register.html',
                                   error_message="Toate câmpurile sunt obligatorii.",
                                   uid=card_uid), 400

        if Person.query.filter_by(uid_card=card_uid).first():
            return render_template('student_register.html',
                                   error_message=f"Acest card ({card_uid}) este deja înregistrat.",
                                   uid=card_uid), 400

        # Salvarea datelor
        new_person = Person(uid_card=card_uid, first_name=first_name, last_name=last_name)
        db.session.add(new_person)
        db.session.commit()

        # Afișează succesul pe aceeași pagină (resetând formularul, fără UID pre-completat)
        return render_template('student_register.html',
                               message_success=f"Înregistrare reușită pentru {first_name} {last_name}! Puteți începe scanarea.")

    # 2. Gestionarea Cererii GET (Afișarea formularului)
    # Preia UID-ul din query string (ex: /student_register?uid=A1B2C3D4)
    pre_filled_uid = request.args.get('uid', '').strip().upper()

    return render_template('student_register.html',
                           uid=pre_filled_uid)


# --- RUTA ADMIN: Status Curent (Home) ---
@app.route('/')
def home():
    people = Person.query.order_by(Person.last_name).all()
    message_success = request.args.get('message_success')
    total_people = len(people)
    people_inside = sum(1 for p in people if p.is_inside)

    return render_template('status.html',
                           people=people,
                           total_people=total_people,
                           people_inside=people_inside,
                           message_success=message_success)


# --- RUTA ADMIN: Profil Persoană ---
@app.route('/profile/<int:person_id>')
def view_person_profile(person_id):
    person = Person.query.get_or_404(person_id)

    logs_for_calculation = Log.query.filter_by(person_id=person_id).order_by(Log.timestamp.asc()).all()
    display_logs = Log.query.filter_by(person_id=person_id).order_by(Log.timestamp.desc()).all()

    sessions = calculate_session_durations(logs_for_calculation)

    finalized_sessions = [s for s in sessions if s.get('is_current') is not True and s.get('end') is not None]
    total_sessions = len(finalized_sessions)

    total_duration_sum = sum((s['duration'] for s in finalized_sessions), timedelta())

    current_session_duration = next((s['duration'] for s in sessions if s.get('is_current')), timedelta())
    total_duration_with_current = total_duration_sum + current_session_duration

    return render_template('profile.html',
                           person=person,
                           sessions=sessions,
                           total_sessions=total_sessions,
                           total_duration_sum=format_timedelta(total_duration_sum),
                           total_duration_with_current=format_timedelta(total_duration_with_current),
                           logs=display_logs)


# --- RUTA ADMIN: Vizualizare Loguri ---
@app.route('/logs')
def view_logs():
    logs = Log.query.order_by(Log.timestamp.desc()).all()
    return render_template('logs.html', logs=logs)


# --- RULAREA APLICATIEI ---
if __name__ == '__main__':
    with app.app_context():
        initialize_db()
    # app.run(debug=True)
    app.run(debug=True, host='0.0.0.0')