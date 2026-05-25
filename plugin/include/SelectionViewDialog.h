#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>

class ccMainAppInterface;
class ccGLWindowInterface;
class ccPointCloud;
class ccHObject;

class SelectionViewDialog : public QDialog
{
    Q_OBJECT
public:
    SelectionViewDialog( ccMainAppInterface *app, QWidget *parent );
    ~SelectionViewDialog() override;

    void showCloud( ccPointCloud *cloud );
    void clearCloud();
    ccGLWindowInterface* glWindow() const { return m_glWindow; }

signals:
    void closed();

protected:
    void closeEvent( QCloseEvent *e ) override;

private:
    void createViewButtons( QVBoxLayout *layout );

    ccMainAppInterface  *m_app;
    ccGLWindowInterface *m_glWindow;
    QWidget             *m_glWidget;
    ccHObject           *m_sceneRoot;
    ccPointCloud        *m_displayCloud;
};
