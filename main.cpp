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
#include <iostream>

#define URL_CAMERA                  "rtsp://192.168.0.237"
#define BASE_PATH                   "video"
#define NUMBER_FILES_FOR_PRESON     99

class RTSPRecorder : public QMainWindow {
    Q_OBJECT
public:
    RTSPRecorder(QWidget *parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("Video-reg");

        person_last_name = new QLineEdit();
        person_last_name->setFixedSize(500, 50);
        person_last_name->setMaxLength(30);
        QFont last_name_font = person_last_name->font();
        last_name_font.setPointSize(20);
        person_last_name->setFont(last_name_font);
        QRegularExpressionValidator *validator = new QRegularExpressionValidator(QRegularExpression("^[a-zA-Zа-яА-Я0-9_-]+$"), person_last_name);
        person_last_name->setValidator(validator);

        int button_hight = 50;
        recordButton = new QPushButton("Записать");
        recordButton->setFixedSize(170, button_hight);
        QFont* record_button_text = new QFont();
        record_button_text->setPointSize(20);
        recordButton->setFont(*record_button_text);

        videoWidget = new QVideoWidget();
        int screen_height = QGuiApplication::primaryScreen()->geometry().height();
        videoWidget->setMinimumHeight(screen_height - button_hight - 90);

        // Медиаплеер для вывода RTSP
        mediaPlayer = new QMediaPlayer(this);
        mediaPlayer->setVideoOutput(videoWidget);

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
        controlLayout->addStretch(1);


        // Обработчики кнопок
        connect(recordButton, &QPushButton::clicked, this, &RTSPRecorder::Recording);
        connect(mediaPlayer, &QMediaPlayer::errorOccurred, this, &RTSPRecorder::handleMediaError);
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
            QString file_directory = QString(BASE_PATH) + QString("/") + QDate::currentDate().toString("yyyy-MM-dd");
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
            ffmpegProcess->write("q");
            ffmpegProcess->waitForFinished();
        }
        recordButton->setText("Записать");
        record_state = true;
    }

    void onFfmpegFinished(int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            QMessageBox::information(this, "Готово", "Запись завершена успешно!");
        } else {
            QMessageBox::warning(this, "Ошибка", 
                QString("FFmpeg завершился с ошибкой (код: %1)").arg(exitCode));
        }
    }

    void handleMediaError([[maybe_unused]]QMediaPlayer::Error error) {
        QMessageBox::critical(this, "Ошибка медиаплеера", mediaPlayer->errorString());
    }

private:
    QLineEdit *person_last_name;
    QPushButton *recordButton;
    QProcess *ffmpegProcess;
    QMediaPlayer *mediaPlayer;
    QVideoWidget *videoWidget;
    QString rtspUrl = URL_CAMERA;
    bool record_state = true;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    RTSPRecorder recorder;
    recorder.showMaximized();
    return app.exec();
}

#include "main.moc"
