#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QProcess>
#include <QDebug>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QFileDialog>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QMediaMetaData>
#include <QSettings>
#include <QTimer>
#include <iostream>

#define NUMBER_FILES_FOR_PRESON     99
#define ABOUT   "Программно-аппаратный комплекс CamRecorder, версия 1.0.0"

class MyLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit MyLineEdit(QString keyboard_path, QWidget *parent = nullptr) : QLineEdit(parent), keyboard_path(keyboard_path)
    {
        keyboard_process = new QProcess(this);
    }
    virtual ~MyLineEdit()
    {
        if(keyboard_process != nullptr)
            delete keyboard_process;
    }
protected:
    void mousePressEvent([[maybe_unused]]QMouseEvent *event) override
    {
        if(keyboard_process->state() == QProcess::Running)
            return;
        connect(keyboard_process, &QProcess::errorOccurred, [this](QProcess::ProcessError error){
            QMessageBox::critical(this, "Ошибка", "Ошибка запуска процесса клавиатуры, код: "
                                                      + QString::number(static_cast<int>(error)));
        });
        keyboard_process->start(keyboard_path, {});
        if (!keyboard_process->waitForStarted()) {
            QMessageBox::critical(this, "Ошибка", "Не удалось открыть клавиатуру, код: "
                                                  + QString::number(keyboard_process->exitCode()));
            return;
        }
    }
private:
    QProcess* keyboard_process = nullptr;
    QString keyboard_path;
};

class RTSPRecorder : public QMainWindow {
    Q_OBJECT
public:
    RTSPRecorder(QWidget *parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("CamRecorder");

        QSettings settings("CamRecorder.ini", QSettings::IniFormat);
        rtspUrl = settings.value("Config/URLCamera", "").toString();
        base_path = settings.value("Config/PathSavingVideos", "").toString();
        keyboard_path = settings.value("Config/KeyboardPath", "osk").toString();

        person_last_name = new MyLineEdit(keyboard_path);
        person_last_name->setFixedSize(500, 50);
        person_last_name->setMaxLength(30);
        QFont last_name_font = person_last_name->font();
        last_name_font.setPointSize(20);
        person_last_name->setFont(last_name_font);
        QRegularExpressionValidator *validator = new QRegularExpressionValidator(QRegularExpression("^[\\sa-zA-Zа-яА-Я0-9_-]+$"), person_last_name);
        person_last_name->setValidator(validator);

        int button_hight = 50;
        recordButton = new QPushButton("Записать");
        recordButton->setFixedSize(170, button_hight);
        QFont* record_button_text = new QFont();
        record_button_text->setPointSize(20);
        recordButton->setFont(*record_button_text);
        recordButton->setEnabled(false);

        about_button = new QPushButton();
        about_button->setText("i");
        QFont about_font = about_button->font();
        about_font.setPointSize(20);
        about_button->setFont(about_font);
        about_button->setFixedSize(50, 50);

        videoWidget = new QVideoWidget();
        int screen_height = QGuiApplication::primaryScreen()->geometry().height();
        videoWidget->setMinimumHeight(screen_height - button_hight - settings.value("Config/VerticalCorrection", 90).toInt());

        // Медиаплеер для вывода RTSP
        mediaPlayer = new QMediaPlayer(this);
        mediaPlayer->setVideoOutput(videoWidget);

        dialog = new QFileDialog(this);
        dialog->setFileMode(QFileDialog::Directory);
        dialog->setOption(QFileDialog::ShowDirsOnly, true);
        dialog->setWindowTitle(tr("Выберите папку"));

        path_selection = new QPushButton();
        path_selection->setFixedSize(50, 50);
        QIcon icon("images/path.png");
        path_selection->setIcon(icon);
        path_selection->setIconSize(QSize(24, 24));

        about_dialog = new QDialog();
        about_dialog->setWindowTitle("Информация");
        about_dialog->setMinimumSize(400, 200);
        layout_about_text = new QVBoxLayout();
        text_about = new QLabel();
        text_about->setText(ABOUT);
        about_dialog->setLayout(layout_about_text);
        layout_about_text->addWidget(text_about);
        layout_about_text->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

        QVBoxLayout *mainLayout = new QVBoxLayout();
        QHBoxLayout *controlLayout = new QHBoxLayout();
        QWidget* centralWidget = new QWidget();
        setCentralWidget(centralWidget);
        centralWidget->setLayout(mainLayout);
        mainLayout->addWidget(videoWidget);
        mainLayout->addLayout(controlLayout);
        controlLayout->setAlignment(Qt::AlignBottom | Qt::AlignLeft);
        controlLayout->addWidget(recordButton);
        controlLayout->addWidget(person_last_name);
        controlLayout->addWidget(path_selection);
        controlLayout->addStretch(1);
        controlLayout->addWidget(about_button);

        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &RTSPRecorder::onTimeout);
        timer->setInterval(5000);

        // Обработчики кнопок
        connect(recordButton, &QPushButton::clicked, this, &RTSPRecorder::Recording);
        connect(recordButton, &QPushButton::clicked, this, &RTSPRecorder::ChangeStateRecordButton);
        connect(path_selection, &QPushButton::clicked, this, &RTSPRecorder::PathSelect);
        connect(about_button,  &QPushButton::clicked, this, &RTSPRecorder::PrintAbout);
        connect(mediaPlayer, &QMediaPlayer::errorOccurred, this, &RTSPRecorder::handleMediaError);
        connect(mediaPlayer, &QMediaPlayer::mediaStatusChanged, this, &RTSPRecorder::CameraFound);
        ffmpegProcess = new QProcess(this);
        connect(ffmpegProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &RTSPRecorder::onFfmpegFinished);
        mediaPlayer->setSource(QUrl(rtspUrl));
        mediaPlayer->play();
    }

private slots:

    void Recording() {

        if(record_state)
        {
            QString outputFile = person_last_name->text().trimmed();
            if ( outputFile.isEmpty())
                return;
            if(base_path.endsWith('/'))
                base_path.chop(1);
            QString file_directory = base_path + "/" + QString("video") + QString("/") + QDate::currentDate().toString("yyyy-MM-dd");
            if(!QDir(file_directory).exists())
                QDir().mkpath(file_directory);
            QFileInfoList fileList = QDir(file_directory).entryInfoList(QStringList() << ( outputFile + "*.mp4" ), QDir::Files | QDir::Readable, QDir::Name);
            int number_file = 0;
            for(const auto& file : fileList)
            {
                QString number = file.baseName().mid(outputFile.size() + 1);
                if(number.toInt() >= NUMBER_FILES_FOR_PRESON - 1)
                    return;
                if(number.toInt() >= number_file)
                    number_file = number.toInt() + 1;
            }
            outputFile = file_directory + "/" + outputFile + "_" + QString::number(number_file) + ".mp4";
            QStringList ffmpegArgs = {
                "-i", rtspUrl,
                "-c", "copy",
                outputFile
            };

            ffmpegProcess->start("ffmpeg", ffmpegArgs);

            if (!ffmpegProcess->waitForStarted()) {
                QMessageBox::critical(this, "Ошибка", "Не удалось запустить FFmpeg!");
                return;
            }
            recordButton->setText("Остановить");
            record_state = false;
            return;
        }
        if(ffmpegProcess->state() == QProcess::Running)
        {
            if(!was_error)
                ffmpegProcess->write("q");
            else
                ffmpegProcess->kill();
            ffmpegProcess->waitForFinished();
        }
        recordButton->setText("Записать");
        record_state = true;
    }

    void onFfmpegFinished(int exitCode, QProcess::ExitStatus exitStatus)
    {
        if (exitStatus == QProcess::NormalExit && exitCode == 0)
        {
            QMessageBox::information(this, "Готово", "Запись завершена успешно!");
        }
        else
        {
            QMessageBox::warning(this, "Ошибка", 
                QString("FFmpeg завершился с ошибкой (код: %1)").arg(exitCode));
        }
    }

    void handleMediaError([[maybe_unused]]QMediaPlayer::Error error) {
        if(was_error)
            return;
        was_error = true;
        if(!record_state)
            Recording(); // Если запись идёт, то при потере связи переводим кнопку, файл финализируется сам ffmpeg-ом
        ChangeStateRecordButton();
        QMessageBox::critical(this, "Ошибка", "Нет сигнала");
        timer->start();
    }

    void CameraFound(QMediaPlayer::MediaStatus status)
    {
        if(status == QMediaPlayer::MediaStatus::BufferingMedia)
            connect(person_last_name, &QLineEdit::textChanged, this, &RTSPRecorder::LastNameChanged);
        if(status != QMediaPlayer::MediaStatus::BufferingMedia || !was_error)
            return;
        timer->stop();
        was_error = false;
        LastNameChanged();
    }

    void onTimeout()
    {
        mediaPlayer->stop();
        mediaPlayer->setSource(QUrl());
        mediaPlayer->setSource(QUrl(rtspUrl));
        mediaPlayer->play();
    }

    void LastNameChanged()
    {
        last_name_is_empty = person_last_name->text().isEmpty() ? true : false;
        ChangeStateRecordButton();
    }

    void PathSelect()
    {
        if(dialog->isActiveWindow())
            return;
        if(dialog->exec() == QDialog::Accepted)
        {
            base_path = dialog->selectedFiles().first();
            QSettings settings("CamRecorder.ini", QSettings::IniFormat);
            settings.setValue("Config/PathSavingVideos", base_path);
        }
    }

    void PrintAbout()
    {
        if(about_dialog->isActiveWindow())
            return;
        about_dialog->exec();
    }

private:
    MyLineEdit *person_last_name;
    QPushButton *recordButton;
    QPushButton *path_selection;
    QPushButton *about_button;
    QProcess *ffmpegProcess;
    QMediaPlayer *mediaPlayer;
    QVideoWidget *videoWidget;
    QString rtspUrl;
    QString base_path, keyboard_path;
    QTimer* timer;
    QFileDialog *dialog;
    QDialog *about_dialog;
    QVBoxLayout *layout_about_text;
    QLabel *text_about;
    bool record_state = true;
    bool was_error = false;
    bool last_name_is_empty = true;

    void ChangeStateRecordButton()
    {
        if((was_error || last_name_is_empty) && record_state)
            recordButton->setEnabled(false);
        else
            recordButton->setEnabled(true);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    RTSPRecorder recorder;
    recorder.showMaximized();
    return app.exec();
}

#include "main.moc"
